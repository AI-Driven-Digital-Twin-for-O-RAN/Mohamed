# ════════════════════════════════════════════════════════════════════
#  Single-VM, single-node k3s deployment of the O-RAN platform on AWS.
#  Uses the default VPC + a tightly-scoped Security Group. The instance
#  bootstraps Docker + k3s + helm + the repo via cloud-init (user-data.sh).
# ════════════════════════════════════════════════════════════════════

locals {
  common_tags = merge(var.tags, {
    Name      = "${var.name_prefix}-host"
    ManagedBy = "terraform"
    Module    = "infra/terraform/aws"
  })

  user_data = templatefile("${path.module}/user-data.sh.tftpl", {
    git_repo_url          = var.git_repo_url
    git_repo_branch       = var.git_repo_branch
    auto_shutdown_minutes = var.auto_shutdown_minutes
    deploy_chart_on_boot  = var.deploy_chart_on_boot
  })
}

# ── Latest Ubuntu 22.04 LTS AMI from Canonical ─────────────────────────────
data "aws_ami" "ubuntu_2204" {
  most_recent = true
  owners      = ["099720109477"] # Canonical's official AWS account

  filter {
    name   = "name"
    values = ["ubuntu/images/hvm-ssd/ubuntu-jammy-22.04-amd64-server-*"]
  }
  filter {
    name   = "architecture"
    values = ["x86_64"]
  }
  filter {
    name   = "virtualization-type"
    values = ["hvm"]
  }
}

# ── VPC ────────────────────────────────────────────────────────────────────
data "aws_vpc" "default" {
  default = true
}

data "aws_subnets" "default_public" {
  filter {
    name   = "vpc-id"
    values = [data.aws_vpc.default.id]
  }
}

# ── Security group ─────────────────────────────────────────────────────────
resource "aws_security_group" "host" {
  name        = "${var.name_prefix}-sg"
  description = "Allow SSH and HTTP from the operator; egress all"
  vpc_id      = data.aws_vpc.default.id
  tags        = local.common_tags

  # SSH from the operator only.
  ingress {
    description = "ssh from operator"
    from_port   = 22
    to_port     = 22
    protocol    = "tcp"
    cidr_blocks = [var.operator_cidr]
  }

  # 80 / 443 left open to the world for the GUI when ingress is enabled.
  ingress {
    description = "http"
    from_port   = 80
    to_port     = 80
    protocol    = "tcp"
    cidr_blocks = ["0.0.0.0/0"]
  }
  ingress {
    description = "https"
    from_port   = 443
    to_port     = 443
    protocol    = "tcp"
    cidr_blocks = ["0.0.0.0/0"]
  }

  # k3s API server, restricted to the operator. Use kubectl with --insecure
  # or fetch the kubeconfig over SSH.
  ingress {
    description = "k3s api from operator"
    from_port   = 6443
    to_port     = 6443
    protocol    = "tcp"
    cidr_blocks = [var.operator_cidr]
  }

  egress {
    description = "egress all"
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }
}

# ── EC2 instance (Spot or on-demand) ───────────────────────────────────────
resource "aws_instance" "host" {
  ami                         = data.aws_ami.ubuntu_2204.id
  instance_type               = var.instance_type
  subnet_id                   = data.aws_subnets.default_public.ids[0]
  vpc_security_group_ids      = [aws_security_group.host.id]
  key_name                    = var.key_name
  associate_public_ip_address = true
  user_data                   = local.user_data
  user_data_replace_on_change = true

  root_block_device {
    volume_type           = "gp3"
    volume_size           = var.root_volume_gib
    delete_on_termination = true
    encrypted             = true
  }

  # Request a Spot instance only if asked.
  dynamic "instance_market_options" {
    for_each = var.use_spot ? [1] : []
    content {
      market_type = "spot"
      spot_options {
        max_price                      = var.spot_max_price_usd_per_hour
        spot_instance_type             = "one-time"
        instance_interruption_behavior = "terminate"
      }
    }
  }

  lifecycle {
    ignore_changes = [ami] # Don't recreate the instance just because Canonical pushed a new AMI.
  }

  tags = local.common_tags
}

# ── Static IP ──────────────────────────────────────────────────────────────
resource "aws_eip" "host" {
  instance = aws_instance.host.id
  domain   = "vpc"
  tags     = merge(local.common_tags, { Name = "${var.name_prefix}-eip" })
}
