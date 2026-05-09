# ADR 0006 — Path portability via `PROJECT_ROOT`

**Status**: Accepted
**Date**: 2026-05-08
**Owner**: Mohamed Moustafa

## Context

The first version of the controller and every shell launcher hardcoded
the absolute path `/home/omar_farouk/open-ran-clean/...`. Examples:

```python
# controller.py (before)
ROOT_DIR = "/home/omar_farouk/open-ran-clean/yousef_fathy"
NS3_DIR  = f"{ROOT_DIR}/ns-O-RAN-flexric/mmwave-LENA-oran"
```

```bash
# gru.sh (before)
GUI_V2="/home/omar_farouk/open-ran-clean/5g-gui-v2"
FLEXRIC_BIN="/home/omar_farouk/open-ran-clean/yousef_fathy/flexric/build/.../nearRT-RIC"
```

The repo physically lives at `/home/ops-boy/project/graduation-project/`
on the current machine. None of the launchers ran without first
symlinking the entire tree into the hardcoded location, which:

- requires `sudo` to create `/home/omar_farouk/`,
- breaks the project for any contributor whose username isn't
  `omar_farouk`,
- makes CI impossible (no GitHub runner has that user),
- breaks containerization (different `/app` paths inside images).

## Decision

A single environment variable, `PROJECT_ROOT`, is the source of truth
for the repo location. It is consumed by:

1. **The controller** (`platform/controller/controller.py`):
   ```python
   PROJECT_ROOT = Path(os.environ.get(
       "PROJECT_ROOT",
       Path(__file__).resolve().parent.parent.parent
   )).resolve()
   PLATFORM_DIR = str(PROJECT_ROOT / "platform")
   FLEXRIC_BIN  = os.environ.get("FLEXRIC_BIN", f"{PLATFORM_DIR}/flexric/build/...")
   ```

2. **The launchers** (`tools/gru.sh`, `tools/rl.sh`, `tools/kill_sim.sh`):
   ```bash
   PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
   ```

3. **The Makefile**:
   ```makefile
   PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
   export PROJECT_ROOT
   ```

4. **The Helm chart**: passed to the controller pod as
   `env: { PROJECT_ROOT: /app }` so the same binary works in containers.

Every individual path (`FLEXRIC_BIN`, `NS3_DIR`, `RESULTS_DIR`, etc.)
is also overridable via its own env var, with a sensible default
derived from `PROJECT_ROOT`. This is documented in `.env.example`.

A second variable, `RUNTIME_CSV_HOME`, exists for the same reason —
the C xApps embed `/home/<user>/handover.csv` at compile time and we
need a portable way to point at the writer's home.

## Consequences

**Positive**

- The repo runs from any path on any machine: dev laptop, CI runner,
  inside a container. No symlink, no sudo.
- The `controller-deployment.yaml` Helm template just sets
  `PROJECT_ROOT=/app` and the same controller code works in cluster.
- New contributors clone, run `make install`, run `make controller` —
  done.
- Phase 1's repo restructure (`5g-gui-v2/` → `platform/{controller,gui}/`)
  was a five-minute change because every path resolved through
  `PROJECT_ROOT`.

**Negative**

- The detection logic differs slightly per consumer:
  - Controller: walk three directories up from `__file__`.
  - Tools scripts: walk one directory up from `BASH_SOURCE`.
  - Makefile: directory containing the Makefile.

  All three converge on the repo root, but the asymmetry is a small
  papercut for first-time readers.
- One env var is not bulletproof. If the controller is run from a
  surprising working directory and `PROJECT_ROOT` isn't exported,
  the default may resolve incorrectly. Documented in `.env.example`.

## Alternatives considered

1. **Symlink `/home/omar_farouk/open-ran-clean` → repo path.**
   Rejected — needs `sudo`, breaks per-user, doesn't work in containers.
2. **Replace every path with a literal in `.env` checked in to git.**
   Rejected — moves the problem from "username" to "literal path"; same
   class of bug.
3. **A configuration framework (Hydra, Dynaconf).** Rejected as
   over-engineering for ~12 paths in one Python file and three shell
   scripts.
4. **Auto-detect by walking up looking for a marker file (`.git`,
   `Makefile`).** Considered. The current `parent.parent.parent` is
   explicit and works because the controller's location is stable.
   Auto-detect would survive future moves but adds complexity now.
