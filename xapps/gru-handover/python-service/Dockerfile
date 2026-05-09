# syntax=docker/dockerfile:1.6
# ─────────────────────────────────────────────────────────────────────
#  GRU Python prediction service  (mohamed710/xapp-gru-service)
#  Wraps the trained Keras handover model behind a Flask REST API.
#  The C xApp talks to it via LSTM_SERVICE_URL=http://gru-service:5000
#
#  Build context = xapps/gru-handover/ (so model/ + artifacts/ are reachable):
#    docker build -f xapps/gru-handover/python-service/Dockerfile \
#                 -t mohamed710/xapp-gru-service:dev xapps/gru-handover/
# ─────────────────────────────────────────────────────────────────────
FROM python:3.11-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    GRU_PORT=5000

RUN apt-get update \
 && apt-get install -y --no-install-recommends curl tini \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY python-service/requirements.txt ./
RUN pip install -r requirements.txt

COPY python-service/gru_xapp.py ./

# Model + scaler artifacts (from Fares' work, now vendored at xapps/gru-handover/).
COPY model/      ./model/
COPY artifacts/  ./artifacts/

ENV MODEL_PATH=/app/model/handover_model_final.keras \
    SCALER_PATH=/app/artifacts/scaler.joblib \
    CONFIG_PATH=/app/artifacts/config.joblib

RUN useradd --system --uid 10002 --home-dir /app appuser \
 && chown -R appuser:appuser /app
USER appuser

EXPOSE 5000

HEALTHCHECK --interval=15s --timeout=3s --retries=3 --start-period=20s \
    CMD curl -fsS http://127.0.0.1:5000/health || exit 1

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["python", "gru_xapp.py"]
