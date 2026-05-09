# ADR 0005 — `K8S_DEMO_MODE` for orchestration verification

**Status**: Accepted
**Date**: 2026-05-08
**Owner**: Mohamed Moustafa

## Context

The K8s control plane (chart, RBAC, controller K8s API code, registry
ConfigMap) had to be designed and verified **before** the FlexRIC and
ns-3 images existed. Building those images takes 30–60 minutes each
and produces multi-GB artifacts. We could not block the entire K8s
work on having them ready.

Without real images:

- A `Job` referencing `mohamed710/ns3-mmwave:dev` would
  `ImagePullBackOff` and fail Helm's `--wait`.
- A regression in the controller's manifest generation would only
  surface during a live simulation, not during normal CI.
- The smoke test (ADR 0007 / `.github/workflows/smoke.yml`) couldn't
  run without simulating the entire stack — too slow for CI.

## Decision

Add a `K8S_DEMO_MODE` environment variable, defaulting to `true` when
the chart is installed. In demo mode:

- The controller's `create_simulation_job` uses `busybox:1.37` as the
  Pod image and `command=["sh", "-c", "echo '[demo] would run sim …'; sleep 30; echo done"]`.
- `create_xapp_job` does the same with the xApp's intended binary path
  printed in the log line for verification.
- The Job runs to completion in 30 seconds.
- Logs include both the parameters that would have been used and the
  resolved registry data (image, binary, FlexRIC service URL, Python
  service URL).

When the real images exist, set `K8S_DEMO_MODE=false` (or `helm upgrade
--set controller.env.K8S_DEMO_MODE=false`). The same code path then
creates real Jobs.

## Consequences

**Positive**

- We could verify the entire K8s control loop end-to-end (POST
  `/k8s/sim/launch` → Jobs created → Pods scheduled → logs fetched →
  controller correctly parses results) without the FlexRIC/ns-3 images.
- The CI smoke test deploys the chart and runs the demo Jobs in
  ~6 minutes. Without demo mode it would need ~1 hour just to build
  the heavy images first.
- The demo log lines are themselves a debug tool. They print the
  image+binary the registry resolved to and the cluster Service URL
  for the Python service, exposing any registry rendering bug
  immediately.

**Negative**

- Demo mode is a **fake** that proves orchestration but not simulation.
  A bug in the real ns-3 invocation (wrong CLI flag, missing env var)
  is invisible until `K8S_DEMO_MODE=false`.
- One extra branch in `create_simulation_job` and `create_xapp_job`.
  Easy to forget to keep both arms in sync as parameters evolve.

## Alternatives considered

1. **Build the heavy images first, then implement K8s control.**
   Rejected — would have meant 1+ days of FlexRIC + ns-3 build work
   before any K8s code could be exercised. Demo mode unblocks the
   loop.
2. **Use a tiny Python image that mimics ns-3's I/O.** Considered.
   Rejected because busybox is smaller, has no startup time, and the
   demo's only job is to prove "the Pod ran and exited 0."
3. **Tag the demo Pods so Prometheus can filter them out.** Possible
   future improvement; today the demo runs are tagged by the
   `oran.farouk/job-kind` label so dashboards can split if needed.
