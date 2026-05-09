# ADR 0003 — Single FlexRIC image; xApps reuse it via binary path override

**Status**: Accepted
**Date**: 2026-05-08
**Owner**: Mohamed Moustafa

## Context

The upstream FlexRIC build (`platform/flexric/docker/Dockerfile.flexric.ubuntu`)
compiles every C-language xApp shipped with FlexRIC's `examples/xApp/c/`
tree as part of the same `cmake + ninja` invocation. The runtime image
includes:

- `/usr/local/bin/nearRT-RIC` — the RIC binary itself,
- `/usr/local/lib/flexric/*.so` — shared libraries,
- `/usr/local/flexric/xApp/c/handover_gru/xapp_handover_gru` — every C
  xApp binary, including `handover_rl`, `lb_awf`, etc.

We had to decide whether to:

(a) ship one image per xApp (`mohamed710/xapp-gru-cxapp`,
    `mohamed710/xapp-rl-cxapp`, `mohamed710/xapp-lb-cxapp`),
(b) ship one platform image and override the entrypoint per Job,
(c) some hybrid where production xApps live in a separate image but
    the FlexRIC examples reuse the platform image.

## Decision

There is **one platform image** — `mohamed710/flexric:dev` — and every
C xApp Job runs that same image with `command:` overridden to point at
the binary baked in at `/usr/local/flexric/xApp/c/<id>/<binary>`.

The xApp registry surfaces this with two fields per xApp:

```yaml
cxapp:
  image:
    repository: ""        # empty → use platform-wide FlexRIC image
    tag: ""
  binary: /usr/local/flexric/xApp/c/handover_gru/xapp_handover_gru
```

Setting a non-empty `image.repository` for a specific xApp opts it out
into a custom image; the registry pattern in ADR 0001 keeps that
flexible.

The controller's `k8s_client.create_xapp_job` resolves the image via
the registry and sets `command=[binary]` on the Job's Pod template.

## Consequences

**Positive**

- **Build once, deploy many.** A single ~30-minute FlexRIC build
  produces the binaries for the RIC + every shipped C xApp. Three
  separate builds would triple the CI time and disk usage with no
  benefit.
- Image storage is dominated by FlexRIC's transitive deps (libsctp,
  asn1c, mbedtls, etc.). Three images would each carry that ~1.5GB
  base. One image is one copy.
- A FlexRIC update automatically updates every xApp's runtime, since
  they share the build artifacts.
- Custom xApps developed outside the FlexRIC tree can still be
  containerized separately by setting `cxapp.image` to override.

**Negative**

- A bug in any one xApp's C source forces a rebuild and re-push of the
  full FlexRIC image. With separate images the blast radius would be
  per-xApp.
- Image scanning tools see fewer images, so audit reports are coarser.
  Mitigated by labeling Jobs with the xApp ID for filtering.
- Onboarding: a contributor writing a brand-new xApp has to either
  vendor their source into the FlexRIC tree or set up a separate
  image. The README in `xapps/<id>/` documents this.

## Alternatives considered

1. **One image per xApp.** Rejected — see "Positive" above. Trade-off
   only worth it if xApps had truly independent build pipelines.
2. **Slim runtime image with binaries copied in.** Considered — would
   shrink the image by removing build-time deps. Rejected for now
   because the FlexRIC binaries link against libraries that live in
   `/usr/local/lib/flexric/` and pruning would require careful `ldd`
   tracing.
3. **`initContainer` that downloads the right xApp binary at start.**
   Rejected — adds a network round-trip per Job, requires hosting
   binaries somewhere, and complicates air-gapped deployments.
