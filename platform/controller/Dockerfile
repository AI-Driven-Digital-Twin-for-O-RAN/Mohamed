# syntax=docker/dockerfile:1.6
# ─────────────────────────────────────────────────────────────────────
#  FastAPI controller image  (mohamed710/oran-controller)
#  Single-replica orchestrator — exposes :8001 with /healthz, /readyz,
#  /ctrl/* endpoints. After the K8s migration this will call the K8s
#  API instead of subprocess.Popen.
# ─────────────────────────────────────────────────────────────────────
FROM python:3.12-slim AS base

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1

# curl for HEALTHCHECK; tini for proper PID 1 signal handling
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl tini \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt ./
RUN pip install -r requirements.txt

COPY controller.py k8s_client.py generate_plots.py generate_gru_pdf.py ./

# Run as non-root for cluster best-practice. Controller does not need root.
RUN useradd --system --uid 10001 --home-dir /app appuser \
 && chown -R appuser:appuser /app
USER appuser

EXPOSE 8001

HEALTHCHECK --interval=15s --timeout=3s --retries=3 --start-period=10s \
    CMD curl -fsS http://127.0.0.1:8001/healthz || exit 1

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["python", "-m", "uvicorn", "controller:app", \
     "--host", "0.0.0.0", "--port", "8001", "--log-level", "info"]
