#!/bin/bash
# ─────────────────────────────────────────────────────────────────────
#  demo-runbook.sh — end-to-end demo for screenshots / thesis figures.
#
#  Prereq: cloud-init has finished. Verify with:
#      cat /var/log/oran-bootstrap-done   # must exist
#
#  Run from /opt/oran (or wherever the repo is):
#      cd /opt/oran && bash tools/demo-runbook.sh
#
#  The script PAUSES at each screenshot moment so you can capture and
#  press Enter to continue.
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

cd "$(dirname "$0")/.."
PROJECT_ROOT="$(pwd)"
export KUBECONFIG="${KUBECONFIG:-/etc/rancher/k3s/k3s.yaml}"

# ── Helpers ──────────────────────────────────────────────────────────
green()   { printf '\033[32m%s\033[0m\n' "$*"; }
yellow()  { printf '\033[33m%s\033[0m\n' "$*"; }
cyan()    { printf '\033[36m%s\033[0m\n' "$*"; }
banner() {
    echo ""
    echo "════════════════════════════════════════════════════════════════"
    cyan  "  $*"
    echo "════════════════════════════════════════════════════════════════"
}
pause_screenshot() {
    yellow ""
    yellow "📸  SCREENSHOT MOMENT: $1"
    yellow "    → Take it now, then press Enter to continue."
    read -r _
}

# ── Section 0: sanity checks ─────────────────────────────────────────
banner "0. Sanity checks (versions, kubeconfig, repo)"

echo "── Kernel + OS ──"
uname -a
lsb_release -d 2>/dev/null || cat /etc/os-release | head -2

echo ""; echo "── Tools ──"
docker  --version
kubectl version --client --short 2>/dev/null || kubectl version --client
helm    version --short
make    --version | head -1

echo ""; echo "── Repo ──"
git log -1 --format='%h %s (%ar)'

pause_screenshot "Section 0 — VM specs + tool versions"

# ── Section 1: offline validation ────────────────────────────────────
banner "1. Offline validation (no Docker / cluster needed)"

# Make sure Python deps are installed before pytest tries to import controller.
# Idempotent: pip skips already-satisfied requirements.
if ! python3 -c 'import fastapi, prometheus_client, kubernetes' 2>/dev/null; then
    yellow "Installing controller Python deps (one-time, ~30s)..."
    pip3 install --user --quiet -r platform/controller/requirements.txt
    pip3 install --user --quiet pytest 2>/dev/null || true
fi

echo "── Unit tests (controller pure functions) ──"
make test 2>&1 | tail -20

echo ""; echo "── Helm chart lint + render ──"
helm lint charts/oran 2>&1 | tail -3
echo ""
echo "── Resource counts (helm template) ──"
helm template oran charts/oran 2>&1 | grep "^kind:" | sort | uniq -c

echo ""; echo "── make doctor ──"
make doctor 2>&1 | tail -15

pause_screenshot "Section 1 — make test (18 passed) + helm lint + make doctor"

# ── Section 2: bring up the cluster (light images) ───────────────────
banner "2. Local Kubernetes (k3d-equivalent — k3s is already running)"

echo "── Cluster nodes ──"
kubectl get nodes -o wide

echo ""; echo "── Building the 4 light images (~5 min) ──"
make image-build-controller
make image-build-gui
make image-build-rl
# GRU service builds only if Fares' model is present
make image-build-gru || true

echo ""; echo "── Imported images (already in single-node k3s) ──"
docker image ls | grep mohamed710 | head

pause_screenshot "Section 2 — kubectl get nodes + docker image ls"

# ── Section 3: deploy the chart ──────────────────────────────────────
banner "3. Helm install the platform"

helm upgrade --install oran charts/oran \
    --namespace oran --create-namespace \
    --wait --timeout 10m

echo ""; echo "── Pods ──"
kubectl get pods -n oran -o wide

echo ""; echo "── Services ──"
kubectl get svc -n oran

echo ""; echo "── Deployments ──"
kubectl get deploy -n oran

pause_screenshot "Section 3 — kubectl get pods/svc/deploy showing 6 components Running"

# ── Section 4: open the dashboards ───────────────────────────────────
banner "4. Port-forwards (so the browser can hit the services)"

# Kill any leftover forwards.
pkill -f 'kubectl port-forward' 2>/dev/null || true
sleep 1

kubectl port-forward -n oran svc/oran-oran-controller 8001:8001 --address 0.0.0.0 > /tmp/pf-ctrl.log 2>&1 &
kubectl port-forward -n oran svc/oran-oran-gui        3001:80   --address 0.0.0.0 > /tmp/pf-gui.log  2>&1 &
kubectl port-forward -n oran svc/oran-oran-grafana    3000:3000 --address 0.0.0.0 > /tmp/pf-graf.log 2>&1 &
kubectl port-forward -n oran svc/oran-oran-prometheus 9090:9090 --address 0.0.0.0 > /tmp/pf-prom.log 2>&1 &

sleep 5
PUB_IP=$(curl -sS https://ifconfig.me)

cat <<EOF

  📺 Open these URLs from your laptop (replace IP if needed):

    3D GUI       http://${PUB_IP}:3001
    Controller   http://${PUB_IP}:8001/healthz
                 http://${PUB_IP}:8001/metrics
                 http://${PUB_IP}:8001/k8s/xapps
    Grafana      http://${PUB_IP}:3000   (admin / admin)
                 →  Dashboards → "O-RAN platform — overview"
    Prometheus   http://${PUB_IP}:9090
                 →  query: oran_component_up

EOF

pause_screenshot "Section 4 — Open Grafana dashboard + Prometheus targets in browser"

# ── Section 5: probe the K8s control endpoints ───────────────────────
banner "5. Controller K8s endpoints"

echo "── /healthz ──"
curl -sS http://localhost:8001/healthz
echo ""

echo ""; echo "── /k8s/health ──"
curl -sS http://localhost:8001/k8s/health | jq .

echo ""; echo "── /k8s/xapps (the registry) ──"
curl -sS http://localhost:8001/k8s/xapps | jq 'keys, .[\"gru-handover\"]'

echo ""; echo "── /metrics (oran_*) ──"
curl -sS http://localhost:8001/metrics | grep -E '^oran_' | head -15

pause_screenshot "Section 5 — terminal showing /k8s/health + /k8s/xapps + /metrics output"

# ── Section 6: launch a demo simulation ──────────────────────────────
banner "6. Trigger a demo simulation Job pair"

RESP=$(curl -sS -X POST http://localhost:8001/k8s/sim/launch \
    -H 'Content-Type: application/json' \
    -d '{"scenario":"gru_scenario","xapp_id":"gru-handover","sim_time":60}')
echo "$RESP" | jq .

RUN_ID=$(echo "$RESP" | jq -r .run_id)
echo ""
yellow "Run ID: $RUN_ID"
echo ""

echo "── Watch Jobs (will exit when both Complete) ──"
kubectl wait --for=condition=complete -n oran job/sim-${RUN_ID}              --timeout=120s
kubectl wait --for=condition=complete -n oran job/xapp-gru-handover-${RUN_ID} --timeout=120s

echo ""; echo "── Final Job state ──"
kubectl get jobs -n oran -l oran.farouk/run-id=${RUN_ID}

echo ""; echo "── Sim Job log ──"
kubectl logs -n oran job/sim-${RUN_ID}
echo ""; echo "── xApp Job log ──"
kubectl logs -n oran job/xapp-gru-handover-${RUN_ID}

pause_screenshot "Section 6 — Jobs Complete + their logs (proves the K8s control loop)"

# ── Section 7: prometheus + grafana with real metrics ────────────────
banner "7. Metrics flowing to Prometheus + Grafana"

echo "── Prometheus targets ──"
curl -sS http://localhost:9090/api/v1/targets | jq '.data.activeTargets[] | {labels: .labels, health: .health}' | head -30

echo ""; echo "── Sample query: oran_component_up ──"
curl -sS 'http://localhost:9090/api/v1/query?query=oran_component_up' | jq '.data.result[] | {component, value}' | head -25

echo ""; echo "── Sample query: oran_e2_connections ──"
curl -sS 'http://localhost:9090/api/v1/query?query=oran_e2_connections' | jq '.data.result[]'

pause_screenshot "Section 7 — Prometheus targets all UP + Grafana dashboard refreshed (data visible)"

# ── Section 8: Documentation surface ─────────────────────────────────
banner "8. Documentation + ADRs (browsable artifacts)"

echo "── ADRs ──"
ls -la docs/ADR/

echo ""; echo "── First page of ADR 0001 ──"
head -30 docs/ADR/0001-multi-xapp-registry-pattern.md

echo ""; echo "── docs/CLOUD.md exists ──"
ls -la docs/CLOUD.md

echo ""; echo "── Terraform AWS module ──"
ls -la infra/terraform/aws/

pause_screenshot "Section 8 — ls of docs/ADR/, head of an ADR, ls of infra/terraform/aws/"

# ── Section 9: GitHub Actions status ─────────────────────────────────
banner "9. GitHub Actions (the CI proof)"

cat <<EOF

  Open in browser to capture the green CI badges:

    https://github.com/AI-Driven-Digital-Twin-for-O-RAN/Mohamed
    https://github.com/AI-Driven-Digital-Twin-for-O-RAN/Mohamed/actions
    https://github.com/AI-Driven-Digital-Twin-for-O-RAN/Mohamed/tree/main/docs/ADR
    https://github.com/AI-Driven-Digital-Twin-for-O-RAN/Mohamed/tree/main/charts/oran

  The README header shows lint · unit-tests · smoke · build badges.

EOF

pause_screenshot "Section 9 — README on GitHub + Actions tab with green workflows"

# ── Section 10: optional — run a real ns-3 sim ───────────────────────
banner "10. (Optional) Real ns-3 simulation — needs FlexRIC + ns-3 images"

if docker image inspect mohamed710/flexric:dev   >/dev/null 2>&1 \
&& docker image inspect mohamed710/ns3-mmwave:dev >/dev/null 2>&1; then
    green "Both heavy images present. Switching to real mode and re-launching."

    helm upgrade oran charts/oran -n oran \
        --set flexric.enabled=true \
        --set controller.env.K8S_DEMO_MODE=false \
        --wait --timeout 5m

    sleep 5
    REAL_RUN=$(curl -sS -X POST http://localhost:8001/k8s/sim/launch \
        -H 'Content-Type: application/json' \
        -d '{"scenario":"gru_scenario","xapp_id":"gru-handover","sim_time":10}' | jq -r .run_id)

    yellow "Real run ID: $REAL_RUN — this will take many minutes (real ns-3)."
    yellow "Watch with:  kubectl logs -n oran job/sim-${REAL_RUN} -f"

    pause_screenshot "Section 10 — Real sim running, Grafana panels populating with non-zero values"
else
    yellow "Heavy images NOT built yet. To enable Section 10:"
    yellow "    make image-build-heavy        # ~1 hour total"
    yellow "    bash tools/demo-runbook.sh    # re-run, will detect them"
fi

# ── Done ─────────────────────────────────────────────────────────────
banner "✅  Demo complete"

cat <<EOF

  Recap of what you screenshot'd:
    1.  VM specs + tool versions
    2.  make test (18 passed) + helm lint + doctor
    3.  kubectl get pods/svc/deploy
    4.  Grafana dashboard + Prometheus targets in browser
    5.  /k8s/health + /k8s/xapps + /metrics
    6.  Jobs Complete + logs
    7.  Prometheus targets + queries
    8.  ADRs + docs + Terraform module
    9.  GitHub README + Actions
    10. (Optional) real ns-3 sim with populated dashboard

  All saved at /tmp/oran-screenshots/ if you used `import` to capture.

  To stop everything (and save Azure money):
    pkill -f 'kubectl port-forward'
    helm uninstall oran -n oran           # leaves the cluster up
    sudo systemctl stop k3s               # stops k3s entirely
    # Then, on your laptop:
    az vm deallocate --resource-group oran-demo --name oran-demo-vm

  To destroy the VM forever:
    az group delete --name oran-demo --yes --no-wait

EOF
