# Terraform — AWS reference deployment

Single-VM, single-node `k3s` deployment of the O-RAN platform on AWS.
Designed for **demo days and short-lived experiments**, not 24/7 operation.

## Cost shape

| Mode | Instance | Spot? | Approx hourly | Approx 6-hour demo |
|---|---|---|---|---|
| Default | `c5.4xlarge` | off | $0.68 | **$4** |
| Cheap   | `c5.4xlarge` | on  | ~$0.20 | **$1.20** |
| Lite    | `t3.xlarge`  | on  | ~$0.05 | **$0.30** (no real ns-3 sim — orchestration only) |

Plus ~$0.08 / GB-month for the EBS volume while the instance exists,
and zero for the EIP while attached. Stop the instance between sessions
to drop hourly costs to ~zero (you keep paying for the EBS volume).

`terraform destroy` returns to $0.

## Prereqs

```bash
aws configure                              # or set AWS_PROFILE / AWS_REGION
aws ec2 create-key-pair \
   --key-name oran-demo \
   --query 'KeyMaterial' \
   --output text > ~/.ssh/oran-demo.pem
chmod 400 ~/.ssh/oran-demo.pem
```

Get your IP for `operator_cidr`:

```bash
echo "$(curl -s https://ifconfig.me)/32"
```

## Apply

From the repo root:

```bash
make tf-init                              # one-time per machine

# Quick interactive apply, single demo:
make tf-apply \
    TF_VAR_key_name=oran-demo \
    TF_VAR_operator_cidr=$(curl -s https://ifconfig.me)/32 \
    TF_VAR_use_spot=true \
    TF_VAR_auto_shutdown_minutes=240    # auto-stop after 4 hours

# 5-7 minutes later:
terraform output ssh_command              # ready-to-paste
```

## Connect

```bash
# SSH:
ssh -i ~/.ssh/oran-demo.pem ubuntu@$(terraform output -raw public_ip)

# kubectl from your laptop (via the public k3s API):
$(terraform output -raw fetch_kubeconfig_command)
kubectl get nodes

# Helm install (skip if you set deploy_chart_on_boot=true):
cd /opt/oran && helm upgrade --install oran charts/oran -n oran \
    --create-namespace --wait --timeout 10m
```

## Tear down

```bash
make tf-destroy
```

## What's deliberately NOT here

This module is the simplest viable cloud shape for a graduation
project. For real production deployment you'd add:

- Multi-AZ HA (control plane resilience).
- A managed Kubernetes service (EKS) for upgrade ergonomics, at
  ~$73/mo control plane + node group cost — wasteful for this workload.
- An SCTP-aware Network Load Balancer for the FlexRIC E2 endpoint
  (AWS NLB supports SCTP since 2020; GCP/Azure don't natively, hence
  AWS as the example region).
- Cert-manager + a real DNS name for the GUI.
- Velero or equivalent for snapshot backups of `sim-results/` and
  `sim_decisions.db`.
- A dedicated VPC with private subnets and a bastion (rather than
  default VPC + public IP).

These are documented in `docs/CLOUD.md`. Skip them unless you have
a real reason.

## Why AWS specifically

SCTP / 36421 is the E2 transport for O-RAN. AWS Network Load Balancer
**supports SCTP** — useful if you ever expose FlexRIC across AZs or
to external simulators. GCP and Azure require workarounds (host
networking on a single node, or tunneling SCTP over IP). AWS is the
path of least resistance for this workload.
