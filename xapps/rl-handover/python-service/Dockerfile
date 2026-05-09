# syntax=docker/dockerfile:1.6
# ─────────────────────────────────────────────────────────────────────
#  RL Python prediction service  (mohamed710/xapp-rl-service)
#  Wraps the trained DDQN model behind a Flask REST API.
#  The C xApp talks to it via RL_SERVICE_URL=http://rl-service:5001
#
#  Build with context = omar_salama/:
#    docker build -f omar_salama/Dockerfile \
#                 -t mohamed710/xapp-rl-service:dev omar_salama/
# ─────────────────────────────────────────────────────────────────────
FROM python:3.11-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    RL_PORT=5001

RUN apt-get update \
 && apt-get install -y --no-install-recommends curl tini \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Serve-time deps are smaller than the training-time requirements.txt.
# Pinned to torch CPU wheels — no GPU needed for inference.
RUN pip install --extra-index-url https://download.pytorch.org/whl/cpu \
        "torch==2.5.1+cpu" \
        "numpy>=1.24" \
        "flask>=2.3.0" \
        "flask-cors>=4.0.0"

COPY rl_xapp.py    ./
COPY src/          ./src/
COPY models/       ./models/

RUN useradd --system --uid 10003 --home-dir /app appuser \
 && chown -R appuser:appuser /app
USER appuser

EXPOSE 5001

HEALTHCHECK --interval=15s --timeout=3s --retries=3 --start-period=20s \
    CMD curl -fsS http://127.0.0.1:5001/health || curl -fsS http://127.0.0.1:5001/ || exit 1

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["python", "rl_xapp.py"]
