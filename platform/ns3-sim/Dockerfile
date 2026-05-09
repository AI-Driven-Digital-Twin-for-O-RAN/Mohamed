# syntax=docker/dockerfile:1.6
# ─────────────────────────────────────────────────────────────────────
#  ns-3 mmWave O-RAN simulation image  (mohamed710/ns3-mmwave)
#
#  Multi-stage:
#    Stage 1: builder  — install ns-3 build deps, COPY source, run
#                        ./ns3 configure + ./ns3 build (builds e2sim-kpmv3
#                        and the mmwave-LENA-oran scenarios).
#    Stage 2: runtime  — slim base + the compiled scenarios + the Python
#                        wrapper (./ns3) + sim_data_pusher.py.
#
#  Build (from repo root, ~30-60 min, image ~3-4 GB):
#    docker build -f platform/ns3-sim/Dockerfile \
#                 -t mohamed710/ns3-mmwave:dev platform/ns3-sim/
#
#  Run a scenario (the controller passes these args automatically):
#    docker run --rm --network=host \
#      mohamed710/ns3-mmwave:dev \
#      ./ns3 run "scratch/gru_scenario --simTime=60 --N_Ues=20 \
#                 --N_MmWaveEnbNodes=7 --indicationPeriodicity=0.05 \
#                 --e2TermIp=<flexric-host-or-svc>"
# ─────────────────────────────────────────────────────────────────────

# ── Stage 1: build ns-3 + e2sim-kpmv3 ────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
       build-essential cmake ninja-build pkg-config git \
       g++ gcc python3 python3-dev python3-pip \
       libsctp-dev libpcap-dev libsqlite3-dev \
       libxml2-dev libgsl-dev libgcrypt-dev \
       libssl-dev autoconf libtool m4 \
       wget ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Source layout: e2sim-kpmv3 at repo root, mmwave-LENA-oran is the ns-3 tree.
# Build context = platform/ns3-sim/, so paths below are relative to it.
COPY e2sim-kpmv3/      /src/e2sim-kpmv3/
COPY mmwave-LENA-oran/ /src/mmwave-LENA-oran/

# 1. Build e2sim-kpmv3 (the E2 KPM agent ns-3 talks to FlexRIC through).
RUN cd /src/e2sim-kpmv3/e2sim \
 && if [ -f build_e2sim.sh ]; then bash build_e2sim.sh 2; \
    else mkdir -p build && cd build && cmake .. && make -j"$(nproc)"; fi

# 2. Configure + build ns-3 with the mmwave-LENA-oran scenarios.
#    ./ns3 is the official Python wrapper; it generates Cmake + Ninja / Make
#    targets internally. Configure first, then build.
RUN cd /src/mmwave-LENA-oran \
 && python3 ns3 configure --enable-examples --enable-tests=OFF \
 && python3 ns3 build -j"$(nproc)"

# Sanity: list a scenario binary so a build break fails the layer here.
RUN ls -la /src/mmwave-LENA-oran/build/scratch/ | head -20

# ── Stage 2: runtime ────────────────────────────────────────────────
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=UTC \
    PYTHONUNBUFFERED=1

# Only the runtime libs ns-3 dynamically links against.
RUN apt-get update && apt-get install -y --no-install-recommends \
       python3 python3-pip \
       libsctp1 libpcap0.8 libsqlite3-0 libxml2 libgsl27 libgcrypt20 \
       libssl3 libgomp1 \
       curl tini \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /sim

# Bring over the entire built ns-3 tree (includes the build/ dir, the ./ns3
# wrapper, scratch/, scenarios/ etc.). Could be slimmed further with cherry-
# picks, but this keeps the Python wrapper usable for `./ns3 run "..."`.
COPY --from=builder /src/mmwave-LENA-oran /sim/

# The CSV streamer (Python). Lives in mmwave-LENA-oran/sim_data_pusher.py
# already (copied above); leaving as-is.

# Non-root for cluster best practice.
RUN useradd --system --uid 10004 --home-dir /sim simuser \
 && chown -R simuser:simuser /sim
USER simuser

# ns-3 emits CSVs into the working directory by default. Mount /sim/output
# as a volume to capture them.
VOLUME ["/sim/output"]

ENTRYPOINT ["/usr/bin/tini", "--"]
# Default: print help. The controller's K8s Job overrides CMD with the actual
# `./ns3 run "scratch/<scenario> --simTime=… --e2TermIp=…"` invocation.
CMD ["python3", "ns3", "run", "--help"]
