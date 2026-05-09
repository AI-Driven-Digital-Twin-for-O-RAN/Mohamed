{{/*
  Common template helpers for the oran chart.
*/}}

{{/* Chart name (sanitized for k8s resource names). */}}
{{- define "oran.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" -}}
{{- end -}}

{{/* Fully qualified app name: <release>-<chart>. */}}
{{- define "oran.fullname" -}}
{{- if .Values.fullnameOverride -}}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" -}}
{{- else -}}
{{- $name := default .Chart.Name .Values.nameOverride -}}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" -}}
{{- end -}}
{{- end -}}

{{/* Chart label suitable for app.kubernetes.io/version. */}}
{{- define "oran.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" -}}
{{- end -}}

{{/* Standard labels applied to every resource. */}}
{{- define "oran.labels" -}}
helm.sh/chart: {{ include "oran.chart" . }}
app.kubernetes.io/name: {{ include "oran.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
app.kubernetes.io/part-of: oran-platform
{{- end -}}

{{/* Selector labels — narrower than oran.labels (no version). */}}
{{- define "oran.selectorLabels" -}}
app.kubernetes.io/name: {{ include "oran.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end -}}

{{/*
  Resolve an image tag.
  Usage:  {{ include "oran.imageTag" (dict "tag" .Values.controller.image.tag "default" .Values.global.defaultTag) }}
  Returns the explicit tag if non-empty, else the global default.
*/}}
{{- define "oran.imageTag" -}}
{{- if and .tag (ne .tag "") -}}
{{ .tag }}
{{- else -}}
{{ .default }}
{{- end -}}
{{- end -}}

{{/*
  Build a full image ref: <registry>/<repo>:<tag>.
  Usage:
    {{ include "oran.image" (dict
      "registry" .Values.global.registry
      "repo"     .Values.controller.image.repository
      "tag"      .Values.controller.image.tag
      "default"  .Values.global.defaultTag) }}
*/}}
{{- define "oran.image" -}}
{{- $tag := include "oran.imageTag" (dict "tag" .tag "default" .default) -}}
{{ .registry }}/{{ .repo }}:{{ $tag }}
{{- end -}}
