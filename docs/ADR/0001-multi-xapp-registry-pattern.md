# ADR 0001 — Multi-xApp registry pattern

**Status**: Accepted
**Date**: 2026-05-08
**Owner**: Mohamed Moustafa

## Context

The platform hosts three independent xApps today (GRU handover, RL handover,
AWF load balancing) and a clear roadmap to add more. Each xApp has:

- a C-language xApp built into the FlexRIC SDK,
- (optionally) a Python prediction service it calls during decisions,
- its own configuration: `indicationPeriodicity`, expected cell count, E2
  function IDs, etc.

The original launcher pattern hard-coded each xApp into both the controller
(`if is_gru: ... elif is_rl: ...`) and a per-xApp shell script (`gru.sh`,
`rl.sh`). Adding a fourth xApp would have meant editing six files.

We needed a structure where adding xApp N means **one entry in one file**
and zero changes to the orchestration code.

## Decision

The xApp registry is a single map under `.Values.xapps` in the Helm chart.
Each entry is one xApp:

```yaml
xapps:
  gru-handover:
    enabled: true
    pythonService:
      enabled: true
      image:    { repository: xapp-gru-service, tag: "" }
      port:     5000
      replicas: 1
    cxapp:
      image:  { repository: "", tag: "" }   # empty → use FlexRIC image
      binary: /usr/local/flexric/xApp/c/handover_gru/xapp_handover_gru
    config:
      indicationPeriodicity: 0.05
      cells: 7
```

A single Helm template (`templates/xapps.yaml`) iterates this map and
renders Deployment + Service + (optional) HorizontalPodAutoscaler for each
entry where `pythonService.enabled` is true.

A second template (`templates/xapp-registry-configmap.yaml`) renders the
**same data as JSON** into a ConfigMap that gets mounted at
`/etc/oran/xapps.json` inside the controller pod. The controller reads it
at runtime (`k8s_client.read_xapp_registry`) to enumerate available xApps
and resolve image + binary references when creating Jobs.

## Consequences

**Positive**

- Adding xApp N is one block in `values.yaml` and a Docker push.
  Zero template changes, zero controller code changes, zero GUI changes.
- The same data is the source of truth for both K8s manifest generation
  (Helm) and runtime orchestration (controller). The two cannot drift.
- The 3D GUI's scenario picker becomes data-driven: it fetches
  `/k8s/xapps` instead of hardcoding scenario names.
- Each xApp has its own image, version, and release cadence. Different
  team members can ship updates without coordinating.

**Negative**

- One more level of indirection compared to a flat list. New contributors
  have to read `xapp-registry-configmap.yaml` to understand how the
  controller learns about xApps.
- Helm's templating language is awkward for nested iterations; the JSON
  rendering uses `dict` and `set` Helm functions which are non-obvious.

## Alternatives considered

1. **Hardcode each xApp in the controller.** Rejected — doesn't scale to
   N xApps, every change is a controller release.
2. **Custom Resource Definition (`Xapp`) + operator.** Rejected — proper
   pattern for production but adds an operator dependency, watch loops,
   and CRD versioning. Overkill for a graduation project.
3. **Per-xApp sub-chart in `charts/xapps/<id>/`.** Rejected — adding a
   fourth xApp would still be "create a new directory + add to parent
   chart's dependencies". The flat registry is simpler.
4. **Read xApp configs from filesystem (no ConfigMap).** Rejected — the
   controller running in a Pod has no access to the chart's `values.yaml`
   except via a ConfigMap mount. ConfigMap is the K8s-native way.
