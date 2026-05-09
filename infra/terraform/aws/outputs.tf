output "public_ip" {
  description = "Public IPv4 of the host (use for SSH and the K3s API)."
  value       = aws_eip.host.public_ip
}

output "ssh_command" {
  description = "Ready-to-paste SSH command. Assumes your private key matches var.key_name."
  value       = "ssh ubuntu@${aws_eip.host.public_ip}"
}

output "fetch_kubeconfig_command" {
  description = "Pulls the cluster's kubeconfig to your machine and rewrites the API server address."
  value       = "scp ubuntu@${aws_eip.host.public_ip}:/etc/rancher/k3s/k3s.yaml ./oran-kubeconfig.yaml && sed -i.bak 's/127\\.0\\.0\\.1/${aws_eip.host.public_ip}/' ./oran-kubeconfig.yaml && echo 'export KUBECONFIG=$(pwd)/oran-kubeconfig.yaml'"
}

output "instance_id" {
  description = "EC2 instance ID — useful for `aws ec2 stop-instances` to save money between demos."
  value       = aws_instance.host.id
}

output "estimated_hourly_cost_usd" {
  description = "Rough on-demand hourly cost. Spot pricing is typically ~30% of this."
  value = {
    "c5.4xlarge" = "0.68"
    "c5.2xlarge" = "0.34"
    "c5.xlarge"  = "0.17"
    "t3.xlarge"  = "0.166"
    "t3.large"   = "0.083"
  }[var.instance_type]
}
