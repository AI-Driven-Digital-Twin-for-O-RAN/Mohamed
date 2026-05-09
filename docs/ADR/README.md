# Architecture Decision Records

Short markdown records of the significant architectural choices made in
this project. Each ADR captures the **why** so the **what** in the
codebase doesn't have to be re-derived from scratch.

Format follows the standard ADR template: Context → Decision →
Consequences → Alternatives considered.

## Index

| #    | Title                                                                       | Status   |
|------|------------------------------------------------------------------------------|----------|
| 0001 | [Multi-xApp registry pattern](0001-multi-xapp-registry-pattern.md)           | Accepted |
| 0002 | [ns-3 simulation as a Kubernetes Job, not a Deployment](0002-ns3-simulation-as-k8s-job.md) | Accepted |
| 0003 | [Single FlexRIC image; xApps reuse it via binary path override](0003-single-flexric-image.md) | Accepted |
| 0004 | [Dual-mode controller (host orchestrator + K8s API)](0004-dual-mode-controller.md) | Accepted |
| 0005 | [`K8S_DEMO_MODE` for orchestration verification](0005-k8s-demo-mode.md)      | Accepted |
| 0006 | [Path portability via `PROJECT_ROOT`](0006-path-portability-via-project-root.md) | Accepted |

## Reading order for a defense committee

If a reviewer has 10 minutes, read in this order:

1. **0006** — the foundation that made everything else possible.
2. **0001** — the showpiece: how the platform scales to N xApps.
3. **0003** — why we have one image and three xApps, not three images.
4. **0002** — why ns-3 is a `Job`. Important framing for the workload shape.
5. **0004** — why the controller has two API families. Demo-day fallback.
6. **0005** — how we verified the K8s control loop without the heavy images.

## Adding a new ADR

1. Pick the next number in sequence (`NNNN` zero-padded).
2. Copy the structure of an existing ADR — the headings (`## Context`,
   `## Decision`, `## Consequences`, `## Alternatives considered`) are
   the contract.
3. Update this README's index.
4. Status options: `Proposed`, `Accepted`, `Deprecated`, or
   `Superseded by ADR XXXX`.
