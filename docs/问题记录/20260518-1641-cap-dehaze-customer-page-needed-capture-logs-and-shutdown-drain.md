# CAP_DEHAZE customer page needed capture logs and shutdown drain

metadata:
- date: 2026-05-18 16:41:37
- kind: runtime
- status: resolved
- repository: /userdata/alldemo
- summary: CAP_DEHAZE page initially produced no VO capture/log evidence and held output/camera pools on shutdown; page now logs frames, captures VO BMPs, drains CAP output, and lets VI cl...

## Symptom

Baseline log had no cap_frames/cap_dehaze capture evidence. After adding capture, shutdown still logged pool_id=7 and pool_id=2 not returned.

## Reproduction

```bash
stdbuf -oL -eL ./scripts/run_alldemo.sh --only CAP_DEHAZE
```

## Evidence

- Failing log: /tmp/alldemo_cap_dehaze_srs001_final.log showed cap_frames and capture but pool_id=7/pool_id=2 reject.
- Passing log: /tmp/alldemo_cap_dehaze_srs001_ctrlc2.log shows cap_frames to 295, captures at 150/300, grep count 0, and clean cleanup.

## Affected Files

- src/alldemo.c
- README.md
- assets/effect_manifest.json

## Root Cause

CAP_DEHAZE was not included in maybe_capture_module_vo_frame, had no periodic runtime printf, CAP output queue could retain one frame at shutdown, and CAMERA_POOL was destroyed before VI module cleanup returned its owned buffers.

## Fix Or Workaround

Add CAP_DEHAZE capture prefix, periodic cap_frames runtime log, drain MEDIA_CAP_DEHAZE output frames during cleanup, and skip explicit CAMERA_POOL destroy for CPU dehaze pages so MEDIA_SYS_Exit can clean VI first.

## Recurrence Prevention

For CPU-send camera pages, require frame-count logs, VO capture artifacts, and Ctrl+C pool-destroy checks; avoid destroying VI camera pools before VI module cleanup runs.

## Related

- SRS-CAP-DEHAZE-CUSTOMER-PAGE-001
