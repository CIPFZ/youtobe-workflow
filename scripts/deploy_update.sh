#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="${SERVICE_NAME:-av-service}"
HEALTH_URL="${HEALTH_URL:-http://127.0.0.1:8888/healthz}"
MAX_RETRIES="${MAX_RETRIES:-30}"
SLEEP_SECONDS="${SLEEP_SECONDS:-2}"
COMPOSE_FILE="${COMPOSE_FILE:-docker-compose.yml}"
LOCAL_UID="${LOCAL_UID:-$(id -u)}"
LOCAL_GID="${LOCAL_GID:-$(id -g)}"

if ! command -v docker >/dev/null 2>&1; then
  echo "[ERROR] docker command not found"
  exit 1
fi

if ! docker compose version >/dev/null 2>&1; then
  echo "[ERROR] docker compose plugin not available"
  exit 1
fi

if [ ! -f "${COMPOSE_FILE}" ]; then
  echo "[ERROR] compose file not found: ${COMPOSE_FILE}"
  exit 1
fi

CURRENT_IMAGE="$(docker inspect -f '{{.Config.Image}}' "${SERVICE_NAME}" 2>/dev/null || true)"
if [ -n "${CURRENT_IMAGE}" ]; then
  echo "[INFO] current image: ${CURRENT_IMAGE}"
else
  echo "[INFO] no existing container named ${SERVICE_NAME}, deployment will be fresh"
fi

echo "[STEP] pulling latest images..."
LOCAL_UID="${LOCAL_UID}" LOCAL_GID="${LOCAL_GID}" docker compose -f "${COMPOSE_FILE}" pull

echo "[STEP] starting updated container..."
LOCAL_UID="${LOCAL_UID}" LOCAL_GID="${LOCAL_GID}" docker compose -f "${COMPOSE_FILE}" up -d

echo "[STEP] waiting for health endpoint: ${HEALTH_URL}"
for ((i=1; i<=MAX_RETRIES; i++)); do
  if curl -fsS "${HEALTH_URL}" >/dev/null 2>&1; then
    echo "[OK] health check passed"
    exit 0
  fi
  echo "[WAIT] attempt ${i}/${MAX_RETRIES} not ready"
  sleep "${SLEEP_SECONDS}"
done

echo "[ERROR] health check failed after update"
if [ -n "${CURRENT_IMAGE}" ]; then
  echo "[ROLLBACK] rolling back to previous image: ${CURRENT_IMAGE}"
  docker rm -f "${SERVICE_NAME}" >/dev/null 2>&1 || true
  docker run -d --name "${SERVICE_NAME}" \
    --user "${LOCAL_UID}:${LOCAL_GID}" \
    --restart unless-stopped \
    -p 8888:8888 \
    -v "$(pwd)/data/input:/data/input" \
    -v "$(pwd)/data/output:/data/output" \
    "${CURRENT_IMAGE}" >/dev/null

  echo "[ROLLBACK] waiting rollback health check"
  for ((i=1; i<=MAX_RETRIES; i++)); do
    if curl -fsS "${HEALTH_URL}" >/dev/null 2>&1; then
      echo "[OK] rollback succeeded"
      exit 1
    fi
    sleep "${SLEEP_SECONDS}"
  done
fi

echo "[FATAL] rollback failed or no rollback image available"
exit 2
