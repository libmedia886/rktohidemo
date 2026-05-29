#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-recordings/visual_check/pages_$(date +%Y%m%d_%H%M%S)}"
shift || true

PAGES=("$@")
if [ "${#PAGES[@]}" -eq 0 ]; then
  PAGES=(
    VI VPSS VO WBC OSD RGA RESIZE_RGA CSC_RGA CSC_CL CLAHE
    RETINEX RETINEX_OFFLINE TNR_CL WAVELET_NR_CL HIGHLIGHT_SUPPRESS HIGHLIGHT_SUPPRESS_VI
    EIS EIS_VI CAP_DEHAZE CAP_DEHAZE_OFFLINE DCP_FAST_DEHAZE THERMAL CONV_CL
    TRANSFORM VMIX VMIX_RGA BLEND_PYR EDOF_CL MCF_FUSION_CL EXPOSURE_FUSION_CL
    DUALVIEW STEREO_3D PANO AVM AVM2D SVM3D DETECT_NPU SEGMENT_NPU VENC VDEC RTSP_SEND RTSP_RECV
    PIC_IO LICENSE
  )
fi

mkdir -p "$ROOT_DIR/$OUT_DIR"
SUMMARY="$ROOT_DIR/$OUT_DIR/summary.tsv"
printf "page\tstatus\trecorder\tpng\trun_log\trecord_log\n" > "$SUMMARY"

cleanup_page() {
  local pid="$1"
  if [ -n "$pid" ]; then
    kill -INT "$pid" 2>/dev/null || true
    sleep 1
    kill -TERM "$pid" 2>/dev/null || true
  fi
}

choose_record_pools() {
  local run_log="$1"
  local pairs=(
    "14 15" "12 13" "10 11" "6 7" "4 5" "0 3"
  )
  local used
  used="$(grep -ao 'id=[0-9][0-9]*' "$run_log" 2>/dev/null | sed 's/id=//g' | sort -n | uniq | tr '\n' ' ')"
  for pair in "${pairs[@]}"; do
    set -- $pair
    local a="$1"
    local b="$2"
    case " $used " in
      *" $a "*|*" $b "*) ;;
      *) printf '%s %s\n' "$a" "$b"; return 0 ;;
    esac
  done
  printf '14 15\n'
}

for page in "${PAGES[@]}"; do
  safe="$(printf '%s' "$page" | tr '/[:space:]' '___')"
  run_log="$ROOT_DIR/$OUT_DIR/${safe}.run.log"
  rec_log="$ROOT_DIR/$OUT_DIR/${safe}.record.log"
  ff_log="$ROOT_DIR/$OUT_DIR/${safe}.ffmpeg.log"
  h264="$ROOT_DIR/$OUT_DIR/${safe}.h264"
  png="$ROOT_DIR/$OUT_DIR/${safe}.png"

  printf '[%s] run\n' "$page"
  (
    cd "$ROOT_DIR" || exit 1
    exec timeout -s INT -k 3s 14s ./scripts/run_alldemo.sh --only "$page"
  ) > "$run_log" 2>&1 &
  child=$!
  sleep 4
  read -r wbc_pool venc_pool < <(choose_record_pools "$run_log")

  rec_status=0
  (
    cd "$ROOT_DIR" || exit 1
    exec ./build/wbc_h264_record "$h264" 2 1024 1920 30 8000000 - /root/licence.dat "$wbc_pool" "$venc_pool"
  ) > "$rec_log" 2>&1 || rec_status=$?

  cleanup_page "$child"
  wait "$child" 2>/dev/null || true

  ff_status=0
  if [ -s "$h264" ]; then
    ffmpeg -hide_banner -loglevel error -y -i "$h264" -frames:v 1 "$png" > "$ff_log" 2>&1 || ff_status=$?
  else
    ff_status=1
  fi

  status="ok"
  if [ "$rec_status" -ne 0 ] || [ "$ff_status" -ne 0 ] || [ ! -s "$png" ]; then
    status="capture_failed"
  fi
  printf "%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$page" "$status" "$rec_status" "$png" "$run_log" "$rec_log" >> "$SUMMARY"
done

printf 'summary: %s\n' "$SUMMARY"
