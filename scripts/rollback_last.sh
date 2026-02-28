#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="${SERVICE_NAME:-av-service}"
HEALTH_URL="${HEALTH_URL:-http://127.0.0.1:8888/healthz}"
MAX_RETRIES="${MAX_RETRIES:-20}"
SLEEP_SECONDS="${SLEEP_SECONDS:-2}"

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <image_tag>"
  echo "example: $0 ghcr.io/cipfz/youtobe-workflow/av-service:sha-abcdef"
  exit 1
fi

TARGET_IMAGE="$1"

echo "[STEP] rollback to ${TARGET_IMAGE}"
docker rm -f "${SERVICE_NAME}" >/dev/null 2>&1 || true
docker run -d --name "${SERVICE_NAME}" \
  --restart unless-stopped \
  -p 8888:8888 \
  -v "$(pwd)/data/input:/data/input" \
  -v "$(pwd)/data/output:/data/output" \
  "${TARGET_IMAGE}" >/dev/null

for ((i=1; i<=MAX_RETRIES; i++)); do
  if curl -fsS "${HEALTH_URL}" >/dev/null 2>&1; then
    echo "[OK] rollback health check passed"
    exit 0
  fi
  echo "[WAIT] rollback health check ${i}/${MAX_RETRIES}"
  sleep "${SLEEP_SECONDS}"
done

echo "[ERROR] rollback container not healthy"
exit 2
