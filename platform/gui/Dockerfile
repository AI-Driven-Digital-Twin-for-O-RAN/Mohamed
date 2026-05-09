# syntax=docker/dockerfile:1.6
# ─────────────────────────────────────────────────────────────────────
#  3D GUI image  (mohamed710/oran-gui)
#  Multi-stage: Node builds the Vite static bundle, nginx serves it.
#  At runtime the bundle calls the controller at /api/* (proxied by
#  nginx → http://controller:8001) so there are no hardcoded URLs.
# ─────────────────────────────────────────────────────────────────────

# ── Stage 1: build the Vite bundle ───────────────────────────────────
FROM node:20-alpine AS builder

WORKDIR /build

COPY package.json package-lock.json ./
RUN npm ci --silent

COPY index.html vite.config.js ./
COPY src ./src

# CONTROLLER_BASE is baked into the bundle as a fallback. Production
# uses nginx /api proxy so this is rarely consulted.
ARG CONTROLLER_BASE="/api"
ENV VITE_CONTROLLER_BASE=$CONTROLLER_BASE

RUN npm run build

# ── Stage 2: nginx runtime ───────────────────────────────────────────
FROM nginx:1.27-alpine AS runtime

# Custom config: serve static + proxy /api/* to the controller Service.
# The CONTROLLER_HOST env var is templated into nginx.conf at startup
# so the same image works in compose (controller:8001) and in K8s
# (controller.<ns>.svc.cluster.local:8001).
ENV CONTROLLER_HOST=controller \
    CONTROLLER_PORT=8001

COPY --from=builder /build/dist /usr/share/nginx/html
COPY nginx.conf.template /etc/nginx/templates/default.conf.template

EXPOSE 80

HEALTHCHECK --interval=15s --timeout=3s --retries=3 \
    CMD wget -qO- http://127.0.0.1/healthz || exit 1
