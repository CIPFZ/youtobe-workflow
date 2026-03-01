#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:8888}"
VIDEO_INPUT="${VIDEO_INPUT:-/data/input/video.mp4}"
AUDIO_INPUT="${AUDIO_INPUT:-/data/input/audio.m4a}"
WAV_INPUT="${WAV_INPUT:-/data/input/audio.wav}"
MERGE_OUTPUT="${MERGE_OUTPUT:-/data/output/out.mp4}"
SRT_OUTPUT="${SRT_OUTPUT:-/data/output/audio.en.srt}"
MODEL_DIR="${MODEL_DIR:-/models/whisper}"
MODEL_NAME="${MODEL_NAME:-ggml-base.en.bin}"
LANGUAGE="${LANGUAGE:-en}"
MAX_RETRIES="${MAX_RETRIES:-60}"
SLEEP_SECONDS="${SLEEP_SECONDS:-1}"
LOCAL_UID="${LOCAL_UID:-$(id -u)}"
LOCAL_GID="${LOCAL_GID:-$(id -g)}"

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[ERROR] missing command: $1"
    exit 1
  fi
}

poll_task() {
  local task_id="$1"
  local name="$2"

  for ((i=1; i<=MAX_RETRIES; i++)); do
    local status
    status="$(curl -fsS "${BASE_URL}/api/v1/task?task_id=${task_id}")"
    echo "[INFO] ${name} task status: ${status}"

    if echo "${status}" | grep -q '"status":"SUCCESS"'; then
      return 0
    fi

    if echo "${status}" | grep -q '"status":"FAILED"'; then
      echo "[ERROR] ${name} task failed"
      return 1
    fi

    sleep "${SLEEP_SECONDS}"
  done

  echo "[ERROR] timeout waiting for ${name} task (${task_id})"
  return 1
}

extract_task_id() {
  sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p'
}

require_cmd docker
require_cmd curl
require_cmd ffmpeg

mkdir -p data/input data/output models/whisper

echo "[STEP] docker compose up --build -d"
LOCAL_UID="${LOCAL_UID}" LOCAL_GID="${LOCAL_GID}" docker compose up --build -d

echo "[STEP] health check"
curl -fsS "${BASE_URL}/healthz"

if [ ! -f "data/input/video.mp4" ]; then
  echo "[ERROR] missing input file: data/input/video.mp4"
  exit 1
fi

if [ ! -f "data/input/audio.m4a" ]; then
  echo "[ERROR] missing input file: data/input/audio.m4a"
  exit 1
fi

echo "[STEP] submit merge task"
MERGE_RESP="$(curl -fsS -X POST "${BASE_URL}/api/v1/merge" \
  -H 'Content-Type: application/json' \
  -d '{
    "video_path": "'"${VIDEO_INPUT}"'",
    "audio_path": "'"${AUDIO_INPUT}"'",
    "output_path": "'"${MERGE_OUTPUT}"'"
  }')"

echo "[INFO] merge response: ${MERGE_RESP}"
MERGE_TASK_ID="$(echo "${MERGE_RESP}" | extract_task_id)"
if [ -z "${MERGE_TASK_ID}" ]; then
  echo "[ERROR] unable to parse merge task_id"
  exit 1
fi
poll_task "${MERGE_TASK_ID}" "merge"

echo "[STEP] verify merged file"
ls -lh data/output/out.mp4

echo "[STEP] convert audio to wav 16k mono"
ffmpeg -y -i data/input/audio.m4a -ar 16000 -ac 1 -c:a pcm_s16le data/input/audio.wav

echo "[STEP] submit asr task"
ASR_RESP="$(curl -fsS -X POST "${BASE_URL}/api/v1/asr" \
  -H 'Content-Type: application/json' \
  -d '{
    "audio_path": "'"${WAV_INPUT}"'",
    "subtitle_path": "'"${SRT_OUTPUT}"'",
    "model_dir": "'"${MODEL_DIR}"'",
    "model_name": "'"${MODEL_NAME}"'",
    "language": "'"${LANGUAGE}"'"
  }')"

echo "[INFO] asr response: ${ASR_RESP}"
ASR_TASK_ID="$(echo "${ASR_RESP}" | extract_task_id)"
if [ -z "${ASR_TASK_ID}" ]; then
  echo "[ERROR] unable to parse asr task_id"
  exit 1
fi
poll_task "${ASR_TASK_ID}" "asr"

echo "[STEP] verify subtitle output"
ls -lh data/output/audio.en.srt
head -n 20 data/output/audio.en.srt

echo "[OK] e2e checks completed"
