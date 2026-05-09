# ── Required ────────────────────────────────────────────────────────────────
variable "key_name" {
  description = "Name of an existing EC2 SSH key pair (must already exist in the target region)."
  type        = string
}

variable "operator_cidr" {
  description = "CIDR block allowed to SSH and reach k3s API. Use https://ifconfig.me/ + /32. NEVER use 0.0.0.0/0."
  type        = string
  validation {
    condition     = can(cidrhost(var.operator_cidr, 0))
    error_message = "operator_cidr must be a valid CIDR block (e.g. 1.2.3.4/32)."
  }
}

# ── Region + sizing ─────────────────────────────────────────────────────────
variable "region" {
  description = "AWS region. NLB-with-SCTP support is regional; us-east-1, eu-west-1, ap-southeast-1 are well-tested."
  type        = string
  default     = "eu-west-1"
}

variable "instance_type" {
  description = "EC2 instance shape. c5.4xlarge (16 vCPU, 32 GB) suits one ns-3 simulation comfortably."
  type        = string
  default     = "c5.4xlarge"
}

variable "use_spot" {
  description = "Request a Spot instance (~70% cheaper, may be reclaimed). Off-by-default for first runs."
  type        = bool
  default     = false
}

variable "spot_max_price_usd_per_hour" {
  description = "Cap on hourly Spot price. Ignored when use_spot=false."
  type        = string
  default     = "0.30"
}

variable "root_volume_gib" {
  description = "Root EBS gp3 size. Builds for FlexRIC + ns-3 + image cache need ~80 GiB minimum."
  type        = number
  default     = 100
}

# ── Tags + naming ───────────────────────────────────────────────────────────
variable "name_prefix" {
  description = "Prefix added to every resource name and the Name tag."
  type        = string
  default     = "oran-platform"
}

variable "tags" {
  description = "Extra tags merged into every resource."
  type        = map(string)
  default = {
    Project = "5g-oran-graduation"
    Owner   = "Mohamed Moustafa"
  }
}

# ── Behaviour toggles ───────────────────────────────────────────────────────
variable "auto_shutdown_minutes" {
  description = "Schedule shutdown N minutes after boot via systemd timer. 0 disables. Useful for live demos."
  type        = number
  default     = 0
}

variable "deploy_chart_on_boot" {
  description = "When true, user-data runs `helm install oran charts/oran` after the cluster is ready."
  type        = bool
  default     = false
}

variable "git_repo_url" {
  description = "Repo cloned to /opt/oran on first boot. Override only if you've forked this project."
  type        = string
  default     = "https://github.com/AI-Driven-Digital-Twin-for-O-RAN/Mohamed.git"
}

variable "git_repo_branch" {
  description = "Branch to check out on first boot."
  type        = string
  default     = "main"
}
