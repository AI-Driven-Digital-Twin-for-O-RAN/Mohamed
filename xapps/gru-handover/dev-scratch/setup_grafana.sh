#!/bin/bash
# ══════════════════════════════════════════════════════════════════════
#  One-shot script: configure InfluxDB + Grafana for the GRU xApp
#  Run once before starting the simulation.
# ══════════════════════════════════════════════════════════════════════
set -e

INFLUX_URL="http://localhost:8086"
GRAFANA_URL="http://localhost:3000"
GRAFANA_USER="admin"
GRAFANA_PASS="admin"
DB="influx"

echo "[1/4] Creating InfluxDB database '${DB}'..."
curl -sf -XPOST "${INFLUX_URL}/query" \
     --data-urlencode "q=CREATE DATABASE \"${DB}\"" > /dev/null
echo "      OK"

echo "[2/4] Waiting for Grafana..."
for i in $(seq 1 15); do
    if curl -sf "${GRAFANA_URL}/api/health" > /dev/null 2>&1; then
        echo "      Grafana ready"
        break
    fi
    sleep 1
done

echo "[3/4] Adding InfluxDB datasource to Grafana..."
curl -sf -X POST "${GRAFANA_URL}/api/datasources" \
     -H "Content-Type: application/json" \
     -u "${GRAFANA_USER}:${GRAFANA_PASS}" \
     -d '{
       "name":      "InfluxDB-GRU",
       "type":      "influxdb",
       "url":       "'"${INFLUX_URL}"'",
       "access":    "proxy",
       "database":  "'"${DB}"'",
       "isDefault": true
     }' > /dev/null 2>&1 || echo "      (datasource may already exist — OK)"
echo "      Done"

echo "[4/4] Importing GRU dashboard..."
DASHBOARD_JSON=$(cat <<'ENDJSON'
{
  "dashboard": {
    "title": "GRU Handover xApp — Live Metrics",
    "uid":   "gru-xapp-v1",
    "tags":  ["oran","gru","handover"],
    "time":  {"from": "now-10m", "to": "now"},
    "refresh": "5s",
    "panels": [
      {
        "id": 1, "type": "stat", "title": "Total Predictions",
        "gridPos": {"x":0,"y":0,"w":4,"h":4},
        "targets": [{"datasource":"InfluxDB-GRU",
          "query": "SELECT last(\"total_predictions\") FROM \"gru_xapp_totals\"",
          "rawQuery": true}],
        "options": {"reduceOptions": {"calcs": ["lastNotNull"]}}
      },
      {
        "id": 2, "type": "stat", "title": "Handovers Executed",
        "gridPos": {"x":4,"y":0,"w":4,"h":4},
        "targets": [{"datasource":"InfluxDB-GRU",
          "query": "SELECT last(\"handovers_executed\") FROM \"gru_xapp_totals\"",
          "rawQuery": true}],
        "fieldConfig": {"defaults": {"color": {"fixedColor":"green","mode":"fixed"}}}
      },
      {
        "id": 3, "type": "stat", "title": "Ping-Pong Blocked",
        "gridPos": {"x":8,"y":0,"w":4,"h":4},
        "targets": [{"datasource":"InfluxDB-GRU",
          "query": "SELECT last(\"ping_pong_blocks\") FROM \"gru_xapp_totals\"",
          "rawQuery": true}],
        "fieldConfig": {"defaults": {"color": {"fixedColor":"red","mode":"fixed"}}}
      },
      {
        "id": 4, "type": "stat", "title": "Unnecessary Blocked",
        "gridPos": {"x":12,"y":0,"w":4,"h":4},
        "targets": [{"datasource":"InfluxDB-GRU",
          "query": "SELECT last(\"unnecessary_blocks\") FROM \"gru_xapp_totals\"",
          "rawQuery": true}],
        "fieldConfig": {"defaults": {"color": {"fixedColor":"orange","mode":"fixed"}}}
      },
      {
        "id": 5, "type": "timeseries", "title": "Per-UE: Should Handover",
        "gridPos": {"x":0,"y":4,"w":12,"h":8},
        "targets": [{"datasource":"InfluxDB-GRU",
          "query": "SELECT mean(\"should_handover\") FROM /gru_decision_ue_.*/ WHERE $timeFilter GROUP BY time(1s), \"measurement\" fill(previous)",
          "rawQuery": true, "alias": "$measurement"}]
      },
      {
        "id": 6, "type": "timeseries", "title": "Per-UE: GRU Confidence",
        "gridPos": {"x":12,"y":4,"w":12,"h":8},
        "targets": [{"datasource":"InfluxDB-GRU",
          "query": "SELECT mean(\"confidence\") FROM /gru_decision_ue_.*/ WHERE $timeFilter GROUP BY time(1s), \"measurement\" fill(previous)",
          "rawQuery": true, "alias": "$measurement"}],
        "fieldConfig": {"defaults": {"min": 0, "max": 1}}
      },
      {
        "id": 7, "type": "timeseries", "title": "Per-UE: Predicted Time-of-Stay (seconds)",
        "gridPos": {"x":0,"y":12,"w":12,"h":8},
        "targets": [{"datasource":"InfluxDB-GRU",
          "query": "SELECT mean(\"time_to_handover\") FROM /gru_decision_ue_.*/ WHERE $timeFilter GROUP BY time(1s), \"measurement\" fill(previous)",
          "rawQuery": true, "alias": "$measurement"}],
        "options": {"tooltip": {"mode": "multi"}},
        "fieldConfig": {"defaults": {"unit": "s"}}
      },
      {
        "id": 8, "type": "timeseries", "title": "Per-UE: RSRP Slope (dBm/s)",
        "gridPos": {"x":12,"y":12,"w":12,"h":8},
        "targets": [{"datasource":"InfluxDB-GRU",
          "query": "SELECT mean(\"rsrp_slope\") FROM /gru_decision_ue_.*/ WHERE $timeFilter GROUP BY time(1s), \"measurement\" fill(previous)",
          "rawQuery": true, "alias": "$measurement"}],
        "fieldConfig": {"defaults": {"unit": "dBm/s"}}
      },
      {
        "id": 9, "type": "timeseries", "title": "Cumulative Handover Stats",
        "gridPos": {"x":0,"y":20,"w":24,"h":8},
        "targets": [
          {"datasource":"InfluxDB-GRU",
           "query": "SELECT last(\"handovers_executed\") FROM \"gru_xapp_totals\" WHERE $timeFilter GROUP BY time(5s) fill(previous)",
           "rawQuery": true, "alias": "Executed"},
          {"datasource":"InfluxDB-GRU",
           "query": "SELECT last(\"ping_pong_blocks\") FROM \"gru_xapp_totals\" WHERE $timeFilter GROUP BY time(5s) fill(previous)",
           "rawQuery": true, "alias": "PP Blocked"},
          {"datasource":"InfluxDB-GRU",
           "query": "SELECT last(\"unnecessary_blocks\") FROM \"gru_xapp_totals\" WHERE $timeFilter GROUP BY time(5s) fill(previous)",
           "rawQuery": true, "alias": "Unnec Blocked"}
        ]
      }
    ]
  },
  "overwrite": true,
  "folderId": 0
}
ENDJSON
)
curl -sf -X POST "${GRAFANA_URL}/api/dashboards/db" \
     -H "Content-Type: application/json" \
     -u "${GRAFANA_USER}:${GRAFANA_PASS}" \
     -d "${DASHBOARD_JSON}" > /dev/null
echo "      Dashboard imported"

echo ""
echo "══════════════════════════════════════════════════════════════"
echo "  Grafana dashboard ready:"
echo "  http://localhost:3000/d/gru-xapp-v1"
echo "  Login: admin / admin"
echo ""
echo "  InfluxDB: http://localhost:8086"
echo "══════════════════════════════════════════════════════════════"
