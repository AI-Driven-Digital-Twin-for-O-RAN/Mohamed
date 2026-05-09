# Cloud reference architecture

What the platform looks like deployed to a real cloud, and the
rationale for the shape we picked.

## Topology — the "right size" for this workload

```
                 Internet
                    │
                    ▼  (operator IP only)
          ┌─────────────────────┐
          │  AWS Security Group │  SSH 22, k3s 6443 → operator
          │                     │  HTTP/HTTPS 80/443 → world (GUI)
          └──────────┬──────────┘
                     │
           ┌─────────▼──────────────┐
           │  EC2  c5.4xlarge       │   single-node k3s
           │  + EIP (static IP)     │
           │  + EBS gp3 100 GiB     │
           │                        │
           │  ┌──────────────────┐  │
           │  │  k3s control     │  │
           │  │  + agent on same │  │
           │  │  node            │  │
           │  └──────┬───────────┘  │
           │         │              │
           │  ┌──────▼─────────┐    │
           │  │ oran namespace │    │
           │  │ (helm release) │    │
           │  │                │    │
           │  │  controller    │ /healthz, /metrics
           │  │  gui (×2)      │ nginx → /api → controller
           │  │  flexric       │ SCTP 36421
           │  │  gru-handover  │ python svc :5000
           │  │  rl-handover   │ python svc :5001
           │  │  prometheus    │ scrapes pods via annotation
           │  │  grafana       │ pre-loaded dashboard
           │  │                │
           │  │  per-sim:      │
           │  │  ns3 Job       │ batch
           │  │  xapp-* Job    │ batch
           │  └────────────────┘    │
           └────────────────────────┘
```

## Why not multi-AZ EKS?

The two big "production K8s" patterns are wrong for this project:

| Pattern | Cost / mo (idle) | Why we didn't pick it |
|---|---|---|
| Multi-AZ EKS + 3-node group | $73 control plane + $200+ nodes | ns-3 doesn't benefit from multi-AZ; one node always idle. |
| Self-hosted multi-master kubeadm | EC2 × 3 minimum | Same: workload doesn't scale horizontally. |
| **Single-VM k3s** ✓ | $0–$50 (mostly off) | Fits the workload exactly. |

ns-3 is a single-process simulator. FlexRIC is a single-replica
service. Neither benefits from horizontal scaling. The control plane
cost for a 3-node EKS cluster would be **higher than the simulation
itself.**

## Cost model

Three operating modes, monthly cost assuming the instance is shut
down between active sessions:

| Mode | Active hours / mo | Spot? | Monthly cost |
|---|---|---|---|
| **Defense day only**            | 6 hr     | yes | ~$2 |
| **Weekly demo / iteration**     | 30 hr    | yes | ~$10 |
| **Full-time research workstation** | 730 hr | off | ~$500 |

The spot instance can be reclaimed at ~2 minute notice; for short
demos that's acceptable. For sims that must complete (3 hrs each),
either use on-demand or accept restarting interrupted runs.

## Operational runbook

### Day 1 — first apply (5–10 min)

```bash
aws ec2 create-key-pair --key-name oran-demo \
    --query KeyMaterial --output text > ~/.ssh/oran-demo.pem
chmod 400 ~/.ssh/oran-demo.pem

cd infra/terraform/aws
terraform init
terraform apply \
    -var key_name=oran-demo \
    -var operator_cidr=$(curl -s https://ifconfig.me)/32 \
    -var use_spot=true \
    -var auto_shutdown_minutes=240
```

### Demo day morning

```bash
# If the instance was stopped between sessions:
aws ec2 start-instances --instance-ids $(terraform output -raw instance_id)

# Wait for k3s to come back (the EIP keeps the same address):
ssh ubuntu@$(terraform output -raw public_ip) "kubectl get nodes"

# Fetch the kubeconfig locally:
scp ubuntu@$(terraform output -raw public_ip):/etc/rancher/k3s/k3s.yaml ./oran-kubeconfig.yaml
sed -i.bak "s/127\\.0\\.0\\.1/$(terraform output -raw public_ip)/" ./oran-kubeconfig.yaml
export KUBECONFIG=$PWD/oran-kubeconfig.yaml
kubectl get pods -A
```

### After the demo

```bash
# Stop (preserve the volume + EIP, ~$10/mo idle):
aws ec2 stop-instances --instance-ids $(terraform output -raw instance_id)

# Or destroy completely ($0):
terraform destroy
```

## What this module is NOT

Listed honestly because a defense reviewer will ask:

1. **Not multi-AZ.** A whole-AZ outage takes the platform down. For a
   research workload, acceptable. For production telco RIC, never.
2. **Not externally backed up.** Snapshots happen via EBS lifecycle
   only. `sim-results/` is on the EBS volume; if that's lost you re-run.
3. **Not secured to production standard.** Default VPC, public IP,
   admin/admin Grafana. Tighten for any real deployment.
4. **No autoscaling.** Single node by design — see "Why not multi-AZ
   EKS" above.
5. **No cross-region disaster recovery.** Out of scope.

If the project ever needs any of these, the migration shape is:
single-VM → managed K8s (EKS / GKE / AKS) + RDS for persistent state +
managed Prometheus / Grafana Cloud + cert-manager for TLS. The Helm
chart in this repo runs unchanged on managed K8s — the cloud-side
changes are purely operational.

## Why AWS specifically

The E2 interface uses **SCTP** on port 36421. Of the major clouds:

| Cloud | NLB SCTP support | Native to use? |
|---|---|---|
| **AWS NLB**        | Yes (since 2020) | Yes — `protocol: SCTP` on a Service of type LoadBalancer works. |
| GCP Network LB     | No (TCP/UDP only) | Workaround: hostNetwork, single-node, no LB. |
| Azure Standard LB  | No (TCP/UDP only) | Same workaround. |

For a graduation project that may want to expose FlexRIC across nodes
or AZs in the future, AWS is the path of least resistance. The
Terraform module can be ported to GCP / Azure with ~50 lines of change
(swap `aws_instance` for `google_compute_instance` /
`azurerm_linux_virtual_machine`); the user-data script is unchanged.

## Multi-cloud notes (skipped, but if asked)

- **GCP**: the equivalent is a single `e2-standard-16` Compute Engine VM
  with the same cloud-init script. Use a GCP Firewall instead of
  Security Group. ~30% cheaper than AWS at equivalent vCPU.
- **Azure**: a `Standard_F16s_v2` VM with NSG. Slightly more expensive
  than AWS but better integration with Azure DevOps for the CI side.

For one-shot demo deployments these are all roughly equivalent. The
SCTP support gap above is the only real differentiator.
