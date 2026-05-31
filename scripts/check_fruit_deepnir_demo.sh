#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DURATION="${1:-35}"
OUT_DIR="${2:-recordings/fruit_deepnir_check_$(date +%Y%m%d_%H%M%S)}"
ABS_OUT="$ROOT_DIR/$OUT_DIR"
RUN_LOG="$ABS_OUT/FRUIT_DETECT_NPU.run.log"

mkdir -p "$ABS_OUT"

status=0
(
  cd "$ROOT_DIR" || exit 1
  timeout -s INT -k 3s "$DURATION" ./scripts/run_alldemo.sh --only FRUIT_DETECT_NPU
) > "$RUN_LOG" 2>&1 || status=$?

if [ "$status" -ne 0 ] && [ "$status" -ne 124 ] && [ "$status" -ne 130 ]; then
  printf 'fruit deepNIR check failed: run status=%s log=%s\n' "$status" "$RUN_LOG" >&2
  exit "$status"
fi

fail=0
grep -Fq 'H264(assets/loop/fruit_detect/deepnir_10fruit_640x640.h264)' "$RUN_LOG" || fail=1
grep -Fq 'model=deepnir_10fruit_yolov8n_640_fp.rknn' "$RUN_LOG" || fail=1
grep -Fq 'adapter=yolov8' "$RUN_LOG" || fail=1
grep -Eq 'FRUIT_DETECT_NPU vdec=[1-9][0-9]*' "$RUN_LOG" || fail=1
grep -Eq 'npu=[1-9][0-9]*' "$RUN_LOG" || fail=1
grep -Eq 'osd=[1-9][0-9]*' "$RUN_LOG" || fail=1
grep -Eq 'vo=[1-9][0-9]*' "$RUN_LOG" || fail=1
grep -Eq 'top0=(apple|avocado|blueberry|capsicum|cherry|kiwi|mango|orange|rockmelon|strawberry) ' "$RUN_LOG" || fail=1

if [ "$fail" -ne 0 ]; then
  printf 'fruit deepNIR check failed: expected deepNIR model/input and live detections in %s\n' "$RUN_LOG" >&2
  exit 1
fi

printf 'fruit deepNIR check ok: %s\n' "$RUN_LOG"
