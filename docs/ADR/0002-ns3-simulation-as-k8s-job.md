# ADR 0002 — ns-3 simulation as a Kubernetes Job, not a Deployment

**Status**: Accepted
**Date**: 2026-05-08
**Owner**: Mohamed Moustafa

## Context

A single ns-3 mmWave simulation in this project takes about **3 hours of
wall-clock time per 60 simulated seconds**. It is one heavyweight Linux
process: it computes channel conditions, SINR, and MAC scheduling for
~20 UEs across ~7 cells at a 0.05s reporting cadence, sends KPM
indications to FlexRIC, and writes its results to CSV files when it
finishes.

Important properties:

1. The simulation has a **defined end** (`--simTime` argument).
2. Once it ends, the binary exits and writes final state to disk.
3. Running two simulations in parallel does not speed any single one up
   (single-process, ns-3 has no built-in MPI for this scenario).
4. Re-running with the same args produces the same result.

We needed to decide what Kubernetes primitive ns-3 should run as.

## Decision

Each simulation run is a **Kubernetes `Job`** created on demand by the
controller via `BatchV1Api().create_namespaced_job(...)` when an HTTP
POST hits `/k8s/sim/launch`. The Job carries labels:

```
oran.farouk/job-kind: simulation
oran.farouk/run-id:   r<unix-timestamp>
```

so listing, filtering, and log retrieval per-run are trivial via
`kubectl get jobs -l oran.farouk/run-id=...`.

The Job is paired with a sibling Job for the chosen C xApp (same
`run-id` label).

A `ttl_seconds_after_finished: 3600` ensures completed Jobs and their
Pods are garbage-collected automatically.

## Consequences

**Positive**

- Native retry semantics (`backoff_limit`), completion tracking, and
  TTL-based cleanup come from the Kubernetes API for free.
- `kubectl wait --for=condition=complete job/sim-rXXX` is the canonical
  way to wait — no custom polling loop in the controller.
- Per-run isolation: a stuck simulation kills only its own Pod. Old
  runs never affect new ones.
- Trivially extends to **parameter sweeps** later: launch N Jobs in
  parallel by issuing N POSTs.

**Negative**

- The controller cannot stream a sim's stdout in real time without
  watching the Pod via the K8s API; today it only fetches logs after
  completion.
- Each simulation Job has its own Pod IP that changes per run, so any
  external system that wants to reach an ns-3 instance directly must
  resolve via Service or wait for the Pod to be Ready.
- ns-3 wall-clock duration (~3 hours per 60 sim-seconds) is much longer
  than typical Kubernetes Job timeouts assume. The controller sets no
  active deadline; operators using horizontal autoscaling on the cluster
  must keep the simulator's nodes alive.

## Alternatives considered

1. **`Deployment` with `replicas: 1`.** Rejected — simulations have a
   defined end. A Deployment would restart the pod on exit, looping
   forever.
2. **`StatefulSet`.** Rejected — same restart semantics as Deployment,
   plus stable network identity and ordered replica management we don't
   need.
3. **`CronJob`.** Plausible for periodic regression runs, but the
   default workflow is "user clicks LAUNCH", not "every Sunday at 3am".
   Could be added later layered on top of `Job` (a CronJob template
   that creates Jobs).
4. **Run ns-3 as a sidecar of the controller Pod.** Rejected — couples
   ns-3 lifetime to the controller's, prevents parallel runs, and
   inflates the controller's resource footprint by 16Gi.
5. **Argo Workflows for orchestration.** Considered — proper choice for
   complex DAGs (sim → analyze → train → export). Rejected here
   because the current workflow is two parallel Jobs with no DAG
   complexity. Easy to migrate to later if the workflow grows.
