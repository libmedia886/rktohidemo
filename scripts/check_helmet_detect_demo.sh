#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DURATION="${1:-35}"
OUT_DIR="${2:-recordings/helmet_detect_check_$(date +%Y%m%d_%H%M%S)}"
ABS_OUT="$ROOT_DIR/$OUT_DIR"
RUN_LOG="$ABS_OUT/HELMET_DETECT_NPU.run.log"
H264_PATH="$ROOT_DIR/assets/loop/helmet_detect/gdut_hwd_helmet_640x640.h264"

mkdir -p "$ABS_OUT"

if command -v ffprobe >/dev/null 2>&1; then
  profile="$(ffprobe -v error -select_streams v:0 -show_entries stream=profile -of csv=p=0 "$H264_PATH")"
  has_b_frames="$(ffprobe -v error -select_streams v:0 -show_entries stream=has_b_frames -of csv=p=0 "$H264_PATH")"
  if [ "$profile" != "Constrained Baseline" ] || [ "$has_b_frames" != "0" ]; then
    printf 'helmet detect check failed: H264 must be Constrained Baseline/no-B, got profile=%s has_b_frames=%s\n' "$profile" "$has_b_frames" >&2
    exit 1
  fi
fi

status=0
(
  cd "$ROOT_DIR" || exit 1
  timeout -s INT -k 3s "$DURATION" ./scripts/run_alldemo.sh --only HELMET_DETECT_NPU
) > "$RUN_LOG" 2>&1 || status=$?

if [ "$status" -ne 0 ] && [ "$status" -ne 124 ] && [ "$status" -ne 130 ]; then
  printf 'helmet detect check failed: run status=%s log=%s\n' "$status" "$RUN_LOG" >&2
  exit "$status"
fi

fail=0
grep -Fq 'H264(assets/loop/helmet_detect/gdut_hwd_helmet_640x640.h264)' "$RUN_LOG" || fail=1
grep -Fq 'model=gdut_hwd_helmet_yolov8n_640_fp.rknn' "$RUN_LOG" || fail=1
grep -Fq 'adapter=yolov8' "$RUN_LOG" || fail=1
grep -Eq 'HELMET_DETECT_NPU vdec=[1-9][0-9]*' "$RUN_LOG" || fail=1
grep -Eq 'npu=[1-9][0-9]*' "$RUN_LOG" || fail=1
grep -Eq 'osd=[1-9][0-9]*' "$RUN_LOG" || fail=1
grep -Eq 'vo=[1-9][0-9]*' "$RUN_LOG" || fail=1
grep -Eq 'top0=(blue_helmet|white_helmet|yellow_helmet|red_helmet|no_helmet) ' "$RUN_LOG" || fail=1

if [ "$fail" -ne 0 ]; then
  printf 'helmet detect check failed: expected GDUT-HWD model/input and live detections in %s\n' "$RUN_LOG" >&2
  exit 1
fi

printf 'helmet detect check ok: %s\n' "$RUN_LOG"
