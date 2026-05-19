# DCP_FAST_DEHAZE 4K input lacks downstream frame flow

metadata:
- date: 2026-05-19 11:27:14
- kind: validation
- status: open
- repository: /userdata/alldemo
- summary: DCP_FAST_DEHAZE can start VI at 3840x2160 but does not show downstream DCP frame growth and leaves one output buffer held on shutdown.

## Symptom

Runtime showed MEDIA_VI_SetAttr 3840x2160@30fps, but no DCP frame counter growth was printed; shutdown logged pool_id=7 still had 1 buffer unreturned.

## Reproduction

```bash
timeout -s INT -k 5s 8s ./scripts/run_alldemo.sh --only DCP_FAST_DEHAZE
```

## Evidence

- MEDIA_VI_SetAttr: ok dev=0 device=/dev/video-camera0 3840x2160 fps=30
- pool_id=7仍有1个buffer未归还，拒绝销毁

## Affected Files

- src/alldemo.c
- docs/AI团队/demo验收/ACCEPT-CUSTOMER-DEMO-4K-CAMERA-COMPARE-20260519-001.yaml

## Root Cause

Suspected cause: DCP_FAST_DEHAZE output buffer is not released or drained on the 4K-input CPU-downsample path, so buffer-flow acceptance cannot prove module processing.

## Fix Or Workaround

Pending: inspect process_live_dcp_dehaze output release/drain and add DCP frame logging before re-enabling 4K input for this page.

## Recurrence Prevention

For 4K camera changes, require downstream module frame counters and clean pool destroy; VI frame count alone is not enough.

## Related

- PRODUCT-CUSTOMER-DEMO-ITERATION-003
