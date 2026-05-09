# ════════════════════════════════════════════════════════════════════
#  5G O-RAN Platform — DevOps Makefile
#  Single discoverable entrypoint for the whole stack.
#  Run `make` (or `make help`) to see what's available.
# ════════════════════════════════════════════════════════════════════

PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export PROJECT_ROOT

CONTROLLER_DIR  := $(PROJECT_ROOT)/platform/controller
GUI_DIR         := $(PROJECT_ROOT)/platform/gui
DOCKER_GUI_DIR  := $(PROJECT_ROOT)/platform/2d-gui
NS3_DIR         := $(PROJECT_ROOT)/platform/ns3-sim/mmwave-LENA-oran
FLEXRIC_DIR     := $(PROJECT_ROOT)/platform/flexric
TOOLS_DIR       := $(PROJECT_ROOT)/tools

GRU_XAPP_DIR    := $(PROJECT_ROOT)/xapps/gru-handover
RL_XAPP_DIR     := $(PROJECT_ROOT)/xapps/rl-handover

CONTROLLER_URL  := http://localhost:8001
GUI_URL         := http://localhost:3001
GUI_2D_URL      := http://localhost:8000

SCENARIO        ?= gru
SIM_TIME        ?= 60
N_UES           ?= 20
N_CELLS         ?= 7

VENV            := $(PROJECT_ROOT)/.venv
# Use the venv python if it exists, else system python3 (Makefile picks one
# at parse time; install-py creates the venv first when ensurepip works).
PYTHON          ?= $(if $(wildcard $(VENV)/bin/python),$(VENV)/bin/python,python3)
NPM             ?= npm

# Container registry. Override with `make image-push REGISTRY=ghcr.io/foo`.
REGISTRY        ?= mohamed710
IMAGE_TAG       ?= dev
COMPOSE         := docker compose -f $(PROJECT_ROOT)/docker-compose.dev.yml
export REGISTRY IMAGE_TAG

# Kubernetes / Helm
CHART           := $(PROJECT_ROOT)/charts/oran
HELM_RELEASE    ?= oran
HELM_NS         ?= oran
K3D_CLUSTER     ?= oran-dev
KUBECTL         := kubectl
HELM            := helm

# Cloud (AWS Terraform module)
TF_DIR          := $(PROJECT_ROOT)/infra/terraform/aws
TERRAFORM       := terraform

.DEFAULT_GOAL := help

# ── Help ────────────────────────────────────────────────────────────
.PHONY: help
help:
	@echo ""
	@echo "  5G O-RAN Platform — make targets"
	@echo "  ────────────────────────────────────────────────────────"
	@echo "  make install        Install Python + Node deps for controller & GUI"
	@echo "  make controller     Start FastAPI controller   (port 8001)"
	@echo "  make gui            Start Vite 3D frontend     (port 3001)"
	@echo "  make docker-up      Start 2D backend Docker stack (Influx + Grafana + GUI)"
	@echo "  make docker-down    Stop the Docker stack"
	@echo ""
	@echo "  make up             Run full sim launcher (SCENARIO=gru|rl)"
	@echo "  make down           Kill EVERYTHING (kill_sim.sh)"
	@echo ""
	@echo "  make status         curl /ctrl/status (operational state)"
	@echo "  make health         curl /healthz + /readyz (controller probes)"
	@echo "  make logs           Tail every component log"
	@echo "  make ports          Show what's listening on our ports"
	@echo ""
	@echo "  make build-info     Print FlexRIC + ns-3 build recipe (binaries not in git)"
	@echo "  make doctor         Check tooling + paths + readiness"
	@echo "  make test           Run controller unit tests (pytest)"
	@echo "  make clean          Kill + clear runtime CSVs and /tmp logs"
	@echo ""
	@echo "  Containers:"
	@echo "  make image-build         Build the 4 light images (controller, gui, gru, rl)"
	@echo "  make image-build-flexric Build FlexRIC + every C xApp in one image (~30 min)"
	@echo "  make image-build-ns3     Build ns-3 mmwave + scenarios (~30-60 min)"
	@echo "  make image-build-heavy   Both of the above"
	@echo "  make image-push          Push every locally-built image to \$$REGISTRY (=$(REGISTRY))"
	@echo "  make compose-up          Build + start the dev stack via docker-compose"
	@echo "  make compose-down        Stop + remove the dev stack"
	@echo "  make compose-logs        Tail logs for every dev-stack container"
	@echo ""
	@echo "  Kubernetes (local k3d):"
	@echo "  make k3d-up         Create local k3d cluster + import images"
	@echo "  make k3d-down       Delete the k3d cluster"
	@echo "  make helm-lint      helm lint + helm template (offline validation)"
	@echo "  make deploy         helm upgrade --install (defaults: rel=$(HELM_RELEASE) ns=$(HELM_NS))"
	@echo "  make undeploy       helm uninstall + delete namespace"
	@echo "  make k8s-status     Show pods/services for the release"
	@echo ""
	@echo "  Cloud (AWS Terraform — see infra/terraform/aws/README.md):"
	@echo "  make tf-init        terraform init"
	@echo "  make tf-fmt         terraform fmt"
	@echo "  make tf-validate    terraform validate (offline)"
	@echo "  make tf-plan        terraform plan (needs TF_VAR_key_name and TF_VAR_operator_cidr)"
	@echo "  make tf-apply       terraform apply -auto-approve"
	@echo "  make tf-destroy     terraform destroy -auto-approve"
	@echo ""
	@echo "  Variables:  SCENARIO=$(SCENARIO)  SIM_TIME=$(SIM_TIME)  N_UES=$(N_UES)  N_CELLS=$(N_CELLS)"
	@echo "  Override:   make up SCENARIO=rl SIM_TIME=120 N_UES=30"
	@echo ""

# ── Install ─────────────────────────────────────────────────────────
.PHONY: install install-py install-js venv

venv:
	@if [ ! -d $(VENV) ]; then \
	    if python3 -c 'import ensurepip' 2>/dev/null; then \
	        echo "  Creating venv at $(VENV) ..."; \
	        python3 -m venv $(VENV); \
	        $(VENV)/bin/pip install --quiet --upgrade pip; \
	    else \
	        echo "  python3-venv not installed (ensurepip missing)."; \
	        echo "  Falling back to --user install. To use a real venv:"; \
	        echo "    sudo apt install python3-venv && rm -rf $(VENV) && make venv"; \
	    fi; \
	fi

install: venv install-py install-js

install-py: venv
	@echo "  Installing Python deps for controller..."
	@if [ -x $(VENV)/bin/pip ]; then \
	    $(VENV)/bin/pip install --quiet -r $(CONTROLLER_DIR)/requirements.txt; \
	    echo "  Done — controller deps in $(VENV)"; \
	else \
	    pip install --user --quiet --break-system-packages -r $(CONTROLLER_DIR)/requirements.txt; \
	    echo "  Done — controller deps in ~/.local (user-site)"; \
	fi

install-js:
	@echo "  Installing Node deps for 3D GUI..."
	@cd $(GUI_DIR) && $(NPM) install --silent

# ── Individual components ───────────────────────────────────────────
.PHONY: controller gui docker-up docker-down

controller:
	@echo "  Starting FastAPI controller on :8001 ..."
	@cd $(CONTROLLER_DIR) && PROJECT_ROOT=$(PROJECT_ROOT) \
	    $(PYTHON) -m uvicorn controller:app --host 0.0.0.0 --port 8001 --log-level info

gui:
	@echo "  Starting Vite 3D frontend on :3001 ..."
	@cd $(GUI_DIR) && $(NPM) run dev -- --host

docker-up:
	@echo "  Starting Docker stack (InfluxDB + 2D GUI backend + Grafana)..."
	@cd $(DOCKER_GUI_DIR) && docker compose up -d influxdb gui

docker-down:
	@echo "  Stopping Docker stack..."
	@cd $(DOCKER_GUI_DIR) && docker compose down

# ── Full simulation lifecycle ───────────────────────────────────────
.PHONY: up down restart

up:
	@echo "  Launching $(SCENARIO) scenario (simTime=$(SIM_TIME)s, UEs=$(N_UES), cells=$(N_CELLS))..."
	@PROJECT_ROOT=$(PROJECT_ROOT) bash $(TOOLS_DIR)/$(SCENARIO).sh $(SIM_TIME) $(N_UES) $(N_CELLS)

down:
	@PROJECT_ROOT=$(PROJECT_ROOT) bash $(TOOLS_DIR)/kill_sim.sh

restart: down up

# ── Observability ───────────────────────────────────────────────────
.PHONY: status health logs ports

status:
	@curl -s $(CONTROLLER_URL)/ctrl/status | $(PYTHON) -m json.tool 2>/dev/null \
	    || echo "  Controller not reachable at $(CONTROLLER_URL)"

health:
	@echo "── /healthz ──────────────"
	@curl -s -o /dev/stdout -w "\n  HTTP %{http_code}\n" $(CONTROLLER_URL)/healthz \
	    || echo "  Controller down"
	@echo ""
	@echo "── /readyz ───────────────"
	@curl -s -o /dev/stdout -w "\n  HTTP %{http_code}\n" $(CONTROLLER_URL)/readyz \
	    || echo "  Controller down"

logs:
	@echo "  Tailing /tmp/{flexric,farouk_*}.log + controller.log (Ctrl-C to stop)..."
	@tail -F /tmp/flexric.log /tmp/farouk_ns3.log /tmp/farouk_xapp.log \
	         /tmp/farouk_gru.log /tmp/farouk_rl_xapp.log /tmp/farouk_pusher.log \
	         /tmp/controller.log 2>/dev/null

ports:
	@echo "  TCP ports of interest:"
	@ss -tlnp 2>/dev/null | awk 'NR==1 || /:(3000|3001|5000|5001|8000|8001|8086)\\>/'
	@echo ""
	@echo "  SCTP (FlexRIC E2):"
	@ss -anp 2>/dev/null | grep ':36421' | head -10 || echo "  (none)"

# ── Doctor & build info ─────────────────────────────────────────────
.PHONY: doctor build-info

doctor:
	@echo "  PROJECT_ROOT  : $(PROJECT_ROOT)"
	@echo "  python3       : $$($(PYTHON) --version 2>&1)"
	@echo "  node          : $$(node --version 2>&1 || echo missing)"
	@echo "  npm           : $$($(NPM) --version 2>&1 || echo missing)"
	@echo "  docker        : $$(docker --version 2>&1 || echo missing)"
	@echo "  docker compose: $$(docker compose version 2>&1 || echo missing)"
	@echo ""
	@echo "  Required artifacts:"
	@printf "    %-28s %s\n" "FlexRIC nearRT-RIC bin"  "$$([ -x $(FLEXRIC_DIR)/build/examples/ric/nearRT-RIC ]    && echo OK || echo MISSING — make build-info)"
	@printf "    %-28s %s\n" "ns-3 build dir"          "$$([ -d $(NS3_DIR)/build ]                                && echo OK || echo MISSING — make build-info)"
	@printf "    %-28s %s\n" "GRU C xApp bin"          "$$([ -x $(FLEXRIC_DIR)/build/examples/xApp/c/handover_gru/xapp_handover_gru ] && echo OK || echo MISSING)"
	@printf "    %-28s %s\n" "RL  C xApp bin"          "$$([ -x $(FLEXRIC_DIR)/build/examples/xApp/c/handover_rl/xapp_handover_rl  ] && echo OK || echo MISSING)"
	@printf "    %-28s %s\n" "GRU Python service"      "$$([ -f $(GRU_XAPP_DIR)/python-service/gru_xapp.py ] && echo OK || echo MISSING)"
	@printf "    %-28s %s\n" "RL  Python service"      "$$([ -f $(RL_XAPP_DIR)/python-service/rl_xapp.py ]    && echo OK || echo MISSING)"
	@printf "    %-28s %s\n" "GRU model"               "$$([ -f $(GRU_XAPP_DIR)/model/handover_model_final.keras ] && echo OK || echo MISSING)"
	@echo ""
	@echo "  Try:  make health  (after  make controller  in another shell)"

build-info:
	@echo ""
	@echo "  ════════════════════════════════════════════════════════════════"
	@echo "  Build recipe — FlexRIC + ns-3 + C xApps"
	@echo "  ════════════════════════════════════════════════════════════════"
	@echo ""
	@echo "  System packages (Ubuntu 22.04):"
	@echo "    sudo apt update && sudo apt install -y \\"
	@echo "      build-essential cmake git python3-dev pkg-config \\"
	@echo "      swig libsctp-dev libpcre2-dev libssl-dev \\"
	@echo "      flex bison libpcap-dev"
	@echo ""
	@echo "  FlexRIC (nearRT-RIC + C xApps):"
	@echo "    cd $(FLEXRIC_DIR) && mkdir -p build && cd build"
	@echo "    cmake .. && make -j\$$(nproc)"
	@echo "    # Builds nearRT-RIC + handover_gru + handover_rl + lb_awf"
	@echo ""
	@echo "  ns-3 mmWave O-RAN scenario:"
	@echo "    cd $(NS3_DIR)"
	@echo "    ./ns3 configure --enable-examples"
	@echo "    ./ns3 build -j\$$(nproc)"
	@echo ""
	@echo "  After both builds finish, run:  make doctor"
	@echo "  Expected wall-clock: 30-90 min total."
	@echo ""

# ── Container images ────────────────────────────────────────────────
.PHONY: image-build image-build-controller image-build-gui image-build-gru image-build-rl image-push compose-up compose-up-detach compose-down compose-logs

image-build: image-build-controller image-build-gui image-build-gru image-build-rl
	@echo "  All 4 images tagged $(REGISTRY)/<name>:$(IMAGE_TAG)"

image-build-controller:
	@echo "  Building $(REGISTRY)/oran-controller:$(IMAGE_TAG) ..."
	@docker build \
	    -f $(CONTROLLER_DIR)/Dockerfile \
	    -t $(REGISTRY)/oran-controller:$(IMAGE_TAG) \
	    $(CONTROLLER_DIR)

image-build-gui:
	@echo "  Building $(REGISTRY)/oran-gui:$(IMAGE_TAG) ..."
	@docker build \
	    -f $(GUI_DIR)/Dockerfile \
	    -t $(REGISTRY)/oran-gui:$(IMAGE_TAG) \
	    $(GUI_DIR)

image-build-gru:
	@if [ ! -f $(GRU_XAPP_DIR)/model/handover_model_final.keras ]; then \
	    echo "  SKIPPED — $(GRU_XAPP_DIR)/model/handover_model_final.keras not found."; \
	    echo "  GRU service image needs Fares' trained model."; \
	else \
	    echo "  Building $(REGISTRY)/xapp-gru-service:$(IMAGE_TAG) ..."; \
	    docker build \
	        -f $(GRU_XAPP_DIR)/python-service/Dockerfile \
	        -t $(REGISTRY)/xapp-gru-service:$(IMAGE_TAG) \
	        $(GRU_XAPP_DIR); \
	fi

image-build-rl:
	@echo "  Building $(REGISTRY)/xapp-rl-service:$(IMAGE_TAG) ..."
	@docker build \
	    -f $(RL_XAPP_DIR)/python-service/Dockerfile \
	    -t $(REGISTRY)/xapp-rl-service:$(IMAGE_TAG) \
	    $(RL_XAPP_DIR)/python-service

# ── FlexRIC + ns-3 (heavy — first build is 30-60 min, multi-GB images) ─
.PHONY: image-build-flexric image-build-ns3 image-build-heavy

image-build-flexric:
	@echo "  Building $(REGISTRY)/flexric:$(IMAGE_TAG) (uses upstream Dockerfile.flexric.ubuntu) ..."
	@echo "  This builds nearRT-RIC + every C xApp (handover_gru, handover_rl, lb_awf)"
	@echo "  in one image. ~30 min on a fast machine, ~2 GB result."
	@docker build \
	    -f $(FLEXRIC_DIR)/docker/Dockerfile.flexric.ubuntu \
	    -t $(REGISTRY)/flexric:$(IMAGE_TAG) \
	    $(FLEXRIC_DIR)

image-build-ns3:
	@echo "  Building $(REGISTRY)/ns3-mmwave:$(IMAGE_TAG) ..."
	@echo "  Compiles e2sim-kpmv3 + ns-3 with mmwave-LENA-oran scenarios."
	@echo "  ~30-60 min on a fast machine, ~3-4 GB result."
	@docker build \
	    -f $(PROJECT_ROOT)/platform/ns3-sim/Dockerfile \
	    -t $(REGISTRY)/ns3-mmwave:$(IMAGE_TAG) \
	    $(PROJECT_ROOT)/platform/ns3-sim

image-build-heavy: image-build-flexric image-build-ns3
	@echo "  FlexRIC + ns-3 images built and tagged $(REGISTRY)/<name>:$(IMAGE_TAG)"

image-push:
	@echo "  Pushing all images to $(REGISTRY) (tag=$(IMAGE_TAG)) ..."
	@for img in oran-controller oran-gui xapp-gru-service xapp-rl-service flexric ns3-mmwave; do \
	    if docker image inspect $(REGISTRY)/$$img:$(IMAGE_TAG) >/dev/null 2>&1; then \
	        docker push $(REGISTRY)/$$img:$(IMAGE_TAG) || exit 1; \
	    else \
	        echo "  - skip $$img (not built locally)"; \
	    fi; \
	done
	@echo "  Done."

compose-up:
	@$(COMPOSE) up --build

compose-up-detach:
	@$(COMPOSE) up --build -d
	@echo ""
	@echo "  GUI         → http://localhost:3001"
	@echo "  Controller  → http://localhost:8001"
	@echo "  Grafana     → http://localhost:3000  (admin/admin)"
	@echo "  InfluxDB    → http://localhost:8086"

compose-down:
	@$(COMPOSE) down

compose-logs:
	@$(COMPOSE) logs -f --tail=50

# ── Kubernetes (k3d local cluster + Helm) ───────────────────────────
.PHONY: k3d-up k3d-down k3d-import helm-lint helm-template deploy undeploy k8s-status

k3d-up:
	@if k3d cluster list 2>/dev/null | grep -q "^$(K3D_CLUSTER) "; then \
	    echo "  k3d cluster '$(K3D_CLUSTER)' already exists."; \
	else \
	    echo "  Creating k3d cluster '$(K3D_CLUSTER)' ..."; \
	    k3d cluster create $(K3D_CLUSTER) \
	        --agents 1 \
	        --port "8080:80@loadbalancer" \
	        --k3s-arg "--disable=traefik@server:0"; \
	fi
	@echo ""
	@echo "  Cluster context: k3d-$(K3D_CLUSTER)"
	@$(KUBECTL) cluster-info --context k3d-$(K3D_CLUSTER) | head -2

k3d-down:
	@k3d cluster delete $(K3D_CLUSTER)

k3d-import:
	@echo "  Importing local images into k3d cluster..."
	@for img in oran-controller oran-gui xapp-gru-service xapp-rl-service flexric ns3-mmwave; do \
	    docker image inspect $(REGISTRY)/$$img:$(IMAGE_TAG) >/dev/null 2>&1 \
	        && k3d image import $(REGISTRY)/$$img:$(IMAGE_TAG) -c $(K3D_CLUSTER) \
	        || echo "  - skip $$img (not built locally)"; \
	done

helm-lint:
	@$(HELM) lint $(CHART)
	@echo ""
	@echo "  Rendering chart (template-only, no cluster needed)..."
	@$(HELM) template $(HELM_RELEASE) $(CHART) > /tmp/oran-rendered.yaml \
	    && echo "  ✓ rendered $$(wc -l < /tmp/oran-rendered.yaml) lines → /tmp/oran-rendered.yaml" \
	    || (echo "  ✗ template failed" && exit 1)

helm-template:
	@$(HELM) template $(HELM_RELEASE) $(CHART)

deploy:
	@$(HELM) upgrade --install $(HELM_RELEASE) $(CHART) \
	    --namespace $(HELM_NS) --create-namespace \
	    --wait --timeout 10m

undeploy:
	@$(HELM) uninstall $(HELM_RELEASE) -n $(HELM_NS) || true
	@$(KUBECTL) delete namespace $(HELM_NS) --ignore-not-found

k8s-status:
	@echo "── Pods ──────────────────────────"
	@$(KUBECTL) get pods -n $(HELM_NS) -l app.kubernetes.io/instance=$(HELM_RELEASE) 2>&1
	@echo ""
	@echo "── Services ──────────────────────"
	@$(KUBECTL) get svc -n $(HELM_NS) -l app.kubernetes.io/instance=$(HELM_RELEASE) 2>&1
	@echo ""
	@echo "── Ingress ───────────────────────"
	@$(KUBECTL) get ingress -n $(HELM_NS) -l app.kubernetes.io/instance=$(HELM_RELEASE) 2>&1

# ── Cloud (AWS Terraform) ───────────────────────────────────────────
.PHONY: tf-init tf-fmt tf-validate tf-plan tf-apply tf-destroy

tf-init:
	@cd $(TF_DIR) && $(TERRAFORM) init

tf-fmt:
	@cd $(TF_DIR) && $(TERRAFORM) fmt -recursive

tf-validate:
	@cd $(TF_DIR) && $(TERRAFORM) validate

tf-plan:
	@cd $(TF_DIR) && $(TERRAFORM) plan

tf-apply:
	@cd $(TF_DIR) && $(TERRAFORM) apply -auto-approve

tf-destroy:
	@cd $(TF_DIR) && $(TERRAFORM) destroy -auto-approve

# ── Diagrams (Mermaid → PNG/SVG) ────────────────────────────────────
# Render docs/images/*.mmd to PNG and SVG using mermaid-cli (mmdc).
# `npm install -g @mermaid-js/mermaid-cli` once. PNGs go alongside the
# .mmd source so they render in the README and are usable for the thesis.
.PHONY: diagrams
diagrams:
	@if ! command -v mmdc >/dev/null 2>&1; then \
	    echo "  mmdc not found. Install it first:"; \
	    echo "    npm install -g @mermaid-js/mermaid-cli"; \
	    exit 1; \
	fi
	@for src in $(PROJECT_ROOT)/docs/images/*.mmd; do \
	    [ -f "$$src" ] || continue; \
	    base=$$(basename $$src .mmd); \
	    echo "  rendering $$base..."; \
	    mmdc -i $$src -o $(PROJECT_ROOT)/docs/images/$$base.png \
	         -t dark -b transparent -w 1600 2>/dev/null && echo "    ✓ $$base.png"; \
	    mmdc -i $$src -o $(PROJECT_ROOT)/docs/images/$$base.svg \
	         -t dark -b transparent 2>/dev/null && echo "    ✓ $$base.svg"; \
	done

# ── Tests ───────────────────────────────────────────────────────────
.PHONY: test test-cov

test:
	@$(PYTHON) -m pytest tests/unit/ -v

test-cov:
	@$(PYTHON) -m pytest tests/unit/ -v --cov=controller --cov-report=term-missing

# ── Clean ───────────────────────────────────────────────────────────
.PHONY: clean
clean: down
	@echo "  Clearing /tmp logs..."
	@: > /tmp/flexric.log 2>/dev/null || true
	@for L in /tmp/farouk_ns3.log /tmp/farouk_xapp.log /tmp/farouk_gru.log /tmp/farouk_rl_xapp.log /tmp/farouk_pusher.log /tmp/controller.log; do \
	    : > $$L 2>/dev/null || true; \
	done
	@echo "  Done."
