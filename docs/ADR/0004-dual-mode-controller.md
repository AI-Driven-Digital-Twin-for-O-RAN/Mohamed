# ADR 0004 — Dual-mode controller (host orchestrator + K8s API)

**Status**: Accepted
**Date**: 2026-05-08
**Owner**: Mohamed Moustafa

## Context

The original controller orchestrated simulations as **host subprocesses**.
Endpoints under `/ctrl/*` called `subprocess.Popen(...)` to start
FlexRIC, ns-3, the data pusher, and the C xApp; logs went to
`/tmp/farouk_*.log`; process state was tracked through OS PIDs.

When we moved to Kubernetes, two paths were on the table:

(a) Cut over fully. Replace every `subprocess.Popen` with K8s API calls
    and remove host mode entirely.
(b) Add a parallel K8s code path under `/k8s/*` and select between them
    via configuration.

A full cutover would have been the cleanest end state but had two real
problems:

- **The host workflow still works without a cluster.** A developer with
  no Docker / k3d setup can still run the platform via `make controller`
  and `make up`. Removing host mode would mandate Kubernetes for every
  contributor.
- **Defense-day fallback.** If the K8s cluster fails 30 minutes before
  the live demo, the host-mode launcher and the recorded sim005 results
  are the panic recovery.

## Decision

The controller exposes both surfaces:

| Endpoint family | Mode      | Backend                              |
|-----------------|-----------|--------------------------------------|
| `/ctrl/*`       | host      | `subprocess.Popen`, `/tmp/*.log`     |
| `/k8s/*`        | k8s       | `kubernetes.client.BatchV1Api`       |

The `ORCHESTRATOR_MODE` environment variable picks the **default** for
ambiguous calls (today only `/ctrl/launch-all` and `/k8s/sim/launch`
exist; in the future there may be a single `POST /sim/launch` that
dispatches based on this var).

The Helm chart sets `ORCHESTRATOR_MODE=k8s` and grants the controller
its own `ServiceAccount` + `Role` + `RoleBinding` for `batch/jobs` and
`pods/log`. Outside the cluster, the controller falls back to host mode.

`k8s_client.py` uses `load_incluster_config()` first, then
`load_kube_config()`, so the same image works in cluster, in compose,
and bare on the host (with a kubeconfig).

## Consequences

**Positive**

- Two functional fallback layers for demo day: K8s → host → recorded
  results.
- Local dev iteration without standing up a cluster (compose + host
  controller is the fast loop).
- Migration is incremental: hot-paths can move to K8s while less
  critical endpoints stay on subprocess.
- The same controller image runs everywhere; no separate "host build"
  vs "k8s build".

**Negative**

- Two code paths to maintain and test. Mitigated by `K8S_DEMO_MODE`
  (ADR 0005) which runs the K8s path against busybox stubs.
- Some endpoints exist twice with subtly different semantics. New
  contributors have to know which is canonical for what.
- The `subprocess.Popen` path embeds the host filesystem assumption
  that FlexRIC, ns-3, and Python services are reachable by absolute
  path. The controller has to keep tracking those paths via env vars
  even when running in cluster, where they don't exist.

## Alternatives considered

1. **Pure K8s, drop host mode.** Rejected — see Context. May revisit
   for v2 once the cluster build is bulletproof.
2. **Host-only, defer K8s indefinitely.** Rejected — the multi-xApp
   registry, Prometheus dashboard, and "DevOps mindset" story all
   benefit from K8s. The cluster path is the strategic direction.
3. **Two separate controllers (host-controller and k8s-controller).**
   Rejected — doubles the deployment surface and forces the GUI to
   choose between two API roots. One process with two API families
   is simpler.
4. **Operator (CRD-driven controller).** Rejected for now — proper
   choice for v2 if simulation lifecycles get complex. The current
   imperative `POST /k8s/sim/launch` is sufficient.
