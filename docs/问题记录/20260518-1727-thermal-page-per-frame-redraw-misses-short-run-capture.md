# THERMAL page misses short-run capture when all 16 palettes redraw every frame

metadata:
- date: 2026-05-18 17:27:27
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: THERMAL per-frame 16-mode redraw was too slow and had no frame log, so the short acceptance run produced no capture evidence.

## Symptom

The baseline THERMAL run only reached the startup line, produced no current thermal_vo_*.bmp capture, and did not show cleanup evidence within the timeout.

## Reproduction

```bash
timeout -s INT -k 5s 9s stdbuf -oL -eL ./scripts/run_alldemo.sh --only THERMAL > /tmp/alldemo_thermal_srs001_baseline.log 2>&1
```

## Evidence

- Fixed run /tmp/alldemo_thermal_srs001_fix1.log reached THERMAL frames=300 and generated thermal_vo_000150.bmp plus thermal_vo_000300.bmp.
- Negative grep for 拒绝销毁, VO capture failed, bind failed, failed, 失败 returned 0 in the fixed run.

## Affected Files

- src/alldemo.c
- README.md
- assets/effect_manifest.json

## Root Cause

draw_thermal_mode_grid recolored and rescaled all 16 palette cells on every display frame, although the source image changes only every 3 seconds; the page also lacked a periodic THERMAL frame log.

## Fix Or Workaround

Cache the full THERMAL single-page render by source-image index, copy the cached NV12 page on unchanged frames, and log THERMAL frames, asset index, cache state, modes, CPU/GPU/RGA each second.

## Recurrence Prevention

For image-loop pages with expensive CPU composition, require frame logs plus VO capture in short-run acceptance and cache static page regions by source/effect key instead of redrawing every frame.

## Related

- SRS-THERMAL-CUSTOMER-PAGE-001
