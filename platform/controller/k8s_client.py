"""
k8s_client.py — Kubernetes orchestration for the controller (Phase 4F).

Drives ns-3 simulations and C xApps as K8s Jobs instead of host subprocesses.
Falls back gracefully when running outside a cluster (host mode keeps working).

Public API:
    is_in_cluster()           -> bool
    read_xapp_registry()      -> dict[xapp_id -> xapp config]
    create_simulation_job(...) -> created Job name
    create_xapp_job(...)       -> created Job name
    list_jobs()                -> list of {name, status, ...}
    get_job_status(name)       -> {phase, succeeded, failed, ...}
    get_job_logs(name)         -> str (latest pod's logs)
    delete_job(name)           -> bool
"""

from __future__ import annotations

import json
import os
import time
from typing import Any

# The kubernetes client is imported lazily so the controller can still start
# in environments where it isn't installed (e.g. someone runs the bare script).
try:
    from kubernetes import client, config  # type: ignore
    from kubernetes.client.exceptions import ApiException  # type: ignore
    _HAS_K8S = True
except ImportError:                                                 # pragma: no cover
    _HAS_K8S = False


# Read once from env; the chart sets these from values.yaml.
NAMESPACE       = os.environ.get("KUBE_NAMESPACE", "oran")
RELEASE_NAME    = os.environ.get("HELM_RELEASE",   "oran")
REGISTRY_CM     = os.environ.get("XAPP_REGISTRY_CM", f"{RELEASE_NAME}-oran-xapp-registry")
REGISTRY_FILE   = os.environ.get("XAPP_REGISTRY_FILE", "/etc/oran/xapps.json")

NS3_IMAGE       = os.environ.get("NS3_IMAGE", "mohamed710/ns3-mmwave:dev")
FLEXRIC_IMAGE   = os.environ.get("FLEXRIC_IMAGE", "mohamed710/flexric:dev")
FLEXRIC_SERVICE = os.environ.get("FLEXRIC_SERVICE", "oran-oran-flexric")
FLEXRIC_E2_PORT = int(os.environ.get("FLEXRIC_E2_PORT", 36421))

# When True, generated Jobs use a busybox sleeper so the manifest can be
# verified end-to-end without real ns-3 / xApp images. Phase 4F demo mode.
DEMO_MODE       = os.environ.get("K8S_DEMO_MODE", "true").lower() in ("1", "true", "yes")


# ── Bootstrap ────────────────────────────────────────────────────────────────
_initialised = False
_init_error: str | None = None


def _init_once() -> bool:
    """Idempotently load kube config (in-cluster first, then local kubeconfig)."""
    global _initialised, _init_error
    if _initialised:
        return _init_error is None
    if not _HAS_K8S:
        _init_error = "kubernetes Python client not installed"
        _initialised = True
        return False
    try:
        config.load_incluster_config()
    except Exception:
        try:
            config.load_kube_config()
        except Exception as exc:
            _init_error = f"could not load kube config: {exc!r}"
            _initialised = True
            return False
    _initialised = True
    return True


def is_in_cluster() -> bool:
    """True only when in-cluster ServiceAccount creds are available."""
    return os.path.exists("/var/run/secrets/kubernetes.io/serviceaccount/token")


def is_available() -> tuple[bool, str | None]:
    """Returns (ok, error). Useful for /k8s/health."""
    ok = _init_once()
    return ok, _init_error


# ── Registry ─────────────────────────────────────────────────────────────────
def read_xapp_registry() -> dict[str, Any]:
    """Return the xApp registry as a dict keyed by xapp id.

    Tries (in order): mounted file (REGISTRY_FILE), then ConfigMap via API,
    then an empty dict. Mounted file is preferred — it's a plain volume
    mount that doesn't need API permissions.
    """
    if os.path.isfile(REGISTRY_FILE):
        try:
            with open(REGISTRY_FILE) as f:
                data = json.load(f)
            return data if isinstance(data, dict) else {}
        except Exception:
            pass

    if not _init_once():
        return {}

    try:
        v1 = client.CoreV1Api()
        cm = v1.read_namespaced_config_map(name=REGISTRY_CM, namespace=NAMESPACE)
        raw = (cm.data or {}).get("xapps.json", "{}")
        data = json.loads(raw)
        return data if isinstance(data, dict) else {}
    except ApiException:
        return {}


# ── Job creation ─────────────────────────────────────────────────────────────
LABEL_RELEASE = "app.kubernetes.io/instance"
LABEL_PART_OF = "app.kubernetes.io/part-of"
LABEL_KIND    = "oran.farouk/job-kind"            # simulation | xapp
LABEL_RUN_ID  = "oran.farouk/run-id"
LABEL_XAPP_ID = "oran.farouk/xapp-id"

DEFAULT_LABELS = {
    LABEL_RELEASE: RELEASE_NAME,
    LABEL_PART_OF: "oran-platform",
    "app.kubernetes.io/managed-by": "oran-controller",
}


def _make_job_object(*, name: str, image: str, command: list[str], labels: dict[str, str],
                     env: dict[str, str] | None = None,
                     resources: dict[str, dict[str, str]] | None = None,
                     ttl_seconds_after_finished: int = 3600) -> Any:
    """Build a batch/v1 Job object."""
    env_list = [client.V1EnvVar(name=k, value=str(v)) for k, v in (env or {}).items()]

    container = client.V1Container(
        name="job",
        image=image,
        command=command,
        env=env_list or None,
        resources=client.V1ResourceRequirements(
            requests=(resources or {}).get("requests"),
            limits=(resources or {}).get("limits"),
        ) if resources else None,
    )

    pod_spec = client.V1PodSpec(
        restart_policy="Never",
        containers=[container],
    )

    pod_template = client.V1PodTemplateSpec(
        metadata=client.V1ObjectMeta(labels=labels),
        spec=pod_spec,
    )

    job_spec = client.V1JobSpec(
        template=pod_template,
        backoff_limit=1,
        ttl_seconds_after_finished=ttl_seconds_after_finished,
    )

    return client.V1Job(
        api_version="batch/v1",
        kind="Job",
        metadata=client.V1ObjectMeta(
            name=name, labels=labels, namespace=NAMESPACE,
        ),
        spec=job_spec,
    )


def create_simulation_job(*, run_id: str, scenario: str, sim_time: int,
                          n_ues: int, n_mmwave: int) -> dict[str, Any]:
    """Create a Job that runs ns-3 with the given parameters."""
    if not _init_once():
        return {"error": _init_error}

    name = f"sim-{run_id}"
    labels = {
        **DEFAULT_LABELS,
        LABEL_KIND:   "simulation",
        LABEL_RUN_ID: run_id,
    }

    if DEMO_MODE:
        # Demo: a 30-second sleep that prints what a real sim would do.
        image = "busybox:1.37"
        command = ["sh", "-c",
                   f"echo '[demo] would run scenario={scenario} simTime={sim_time} "
                   f"UEs={n_ues} cells={n_mmwave} e2TermIp={FLEXRIC_SERVICE}'; "
                   f"sleep 30; "
                   f"echo '[demo] sim {run_id} done'"]
        resources = None
    else:
        # Real mode: ns-3 reaches FlexRIC over the cluster Service DNS name.
        image = NS3_IMAGE
        command = ["python3", "ns3", "run",
                   f"scratch/{scenario}.cc"
                   f" --simTime={sim_time}"
                   f" --N_Ues={n_ues}"
                   f" --N_MmWaveEnbNodes={n_mmwave}"
                   f" --indicationPeriodicity=0.05"
                   f" --hoSinrDifference=3"
                   f" --e2TermIp={FLEXRIC_SERVICE}"]
        resources = {
            "requests": {"cpu": "4",  "memory": "8Gi"},
            "limits":   {"cpu": "8",  "memory": "16Gi"},
        }

    job = _make_job_object(name=name, image=image, command=command, labels=labels,
                           resources=resources)

    batch = client.BatchV1Api()
    try:
        batch.create_namespaced_job(namespace=NAMESPACE, body=job)
    except ApiException as exc:
        return {"error": f"create_namespaced_job failed: {exc.reason}", "status": exc.status}

    return {"name": name, "namespace": NAMESPACE, "kind": "simulation",
            "image": image, "labels": labels}


def create_xapp_job(*, run_id: str, xapp_id: str, registry: dict[str, Any]) -> dict[str, Any]:
    """Create a Job that runs the C xApp for the given xapp_id."""
    if not _init_once():
        return {"error": _init_error}

    if xapp_id not in registry:
        return {"error": f"unknown xapp '{xapp_id}'",
                "available": list(registry.keys())}

    entry = registry[xapp_id]
    name = f"xapp-{xapp_id}-{run_id}"
    labels = {
        **DEFAULT_LABELS,
        LABEL_KIND:    "xapp",
        LABEL_RUN_ID:  run_id,
        LABEL_XAPP_ID: xapp_id,
    }

    env: dict[str, str] = {}
    py_url = (entry.get("pythonService") or {}).get("url", "")
    if py_url:
        env["LSTM_SERVICE_URL"] = py_url    # C xApps look for this name today
        env["RL_SERVICE_URL"]   = py_url
    # Tell the xApp where to find FlexRIC. C xApps read this via flexric.conf
    # or env override depending on the binary.
    env["E2TERM_IP"]      = FLEXRIC_SERVICE
    env["FLEXRIC_HOST"]   = FLEXRIC_SERVICE
    env["FLEXRIC_E2_PORT"]= str(FLEXRIC_E2_PORT)

    cxapp     = entry.get("cxapp") or {}
    cx_image  = cxapp.get("image",  "") or FLEXRIC_IMAGE
    cx_binary = cxapp.get("binary", "")

    if DEMO_MODE:
        image = "busybox:1.37"
        command = ["sh", "-c",
                   f"echo '[demo] xapp={xapp_id} would run {cx_binary or '<no-binary>'} "
                   f"in {cx_image} talking to py-svc={py_url or '<none>'} and "
                   f"FlexRIC at {FLEXRIC_SERVICE}:{FLEXRIC_E2_PORT}'; sleep 25; "
                   f"echo '[demo] xapp {xapp_id} done'"]
    else:
        if not cx_binary:
            return {"error": f"xapp '{xapp_id}' has no cxapp.binary in registry"}
        image = cx_image
        # Override the FlexRIC image's default CMD with the specific xApp binary.
        command = [cx_binary]

    job = _make_job_object(name=name, image=image, command=command,
                           labels=labels, env=env)

    try:
        client.BatchV1Api().create_namespaced_job(namespace=NAMESPACE, body=job)
    except ApiException as exc:
        return {"error": f"create_namespaced_job failed: {exc.reason}", "status": exc.status}

    return {"name": name, "namespace": NAMESPACE, "kind": "xapp",
            "xapp_id": xapp_id, "image": image, "labels": labels}


# ── Job inspection ───────────────────────────────────────────────────────────
def list_jobs(run_id: str | None = None) -> list[dict[str, Any]]:
    """List Jobs created by this controller (filter by run_id if given)."""
    if not _init_once():
        return []

    selector = f"{LABEL_PART_OF}=oran-platform,app.kubernetes.io/managed-by=oran-controller"
    if run_id:
        selector += f",{LABEL_RUN_ID}={run_id}"

    try:
        jobs = client.BatchV1Api().list_namespaced_job(
            namespace=NAMESPACE, label_selector=selector,
        )
    except ApiException as exc:
        return [{"error": str(exc)}]

    out = []
    for j in jobs.items:
        st = j.status or client.V1JobStatus()
        out.append({
            "name":      j.metadata.name,
            "kind":      (j.metadata.labels or {}).get(LABEL_KIND, "?"),
            "run_id":    (j.metadata.labels or {}).get(LABEL_RUN_ID, "?"),
            "xapp_id":   (j.metadata.labels or {}).get(LABEL_XAPP_ID),
            "active":    st.active or 0,
            "succeeded": st.succeeded or 0,
            "failed":    st.failed or 0,
            "start_time":      str(st.start_time) if st.start_time else None,
            "completion_time": str(st.completion_time) if st.completion_time else None,
        })
    return out


def get_job_status(name: str) -> dict[str, Any]:
    """Return the latest status for one Job."""
    if not _init_once():
        return {"error": _init_error}
    try:
        j = client.BatchV1Api().read_namespaced_job(name=name, namespace=NAMESPACE)
    except ApiException as exc:
        return {"error": exc.reason, "status": exc.status}
    st = j.status or client.V1JobStatus()
    return {
        "name":      name,
        "active":    st.active or 0,
        "succeeded": st.succeeded or 0,
        "failed":    st.failed or 0,
        "start_time":      str(st.start_time) if st.start_time else None,
        "completion_time": str(st.completion_time) if st.completion_time else None,
        "conditions": [
            {"type": c.type, "status": c.status, "reason": c.reason, "message": c.message}
            for c in (st.conditions or [])
        ],
    }


def get_job_logs(name: str, tail_lines: int = 200) -> str:
    """Return logs from the most recent Pod backing this Job."""
    if not _init_once():
        return f"<error: {_init_error}>"
    v1 = client.CoreV1Api()
    try:
        pods = v1.list_namespaced_pod(
            namespace=NAMESPACE,
            label_selector=f"job-name={name}",
        )
    except ApiException as exc:
        return f"<error: {exc.reason}>"
    if not pods.items:
        return "<no pod yet>"
    pod = sorted(pods.items, key=lambda p: p.metadata.creation_timestamp)[-1]
    try:
        return v1.read_namespaced_pod_log(
            name=pod.metadata.name, namespace=NAMESPACE,
            tail_lines=tail_lines,
        )
    except ApiException as exc:
        return f"<error: {exc.reason}>"


def delete_job(name: str) -> dict[str, Any]:
    """Delete a Job and its dependent Pods."""
    if not _init_once():
        return {"error": _init_error}
    try:
        client.BatchV1Api().delete_namespaced_job(
            name=name, namespace=NAMESPACE,
            propagation_policy="Foreground",
        )
    except ApiException as exc:
        return {"error": exc.reason, "status": exc.status}
    return {"deleted": name}


def new_run_id() -> str:
    return f"r{int(time.time())}"
