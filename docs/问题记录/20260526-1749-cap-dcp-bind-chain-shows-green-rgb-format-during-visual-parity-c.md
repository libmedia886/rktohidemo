# CAP/DCP bind chain shows green RGB format during visual parity check

metadata:
- date: 2026-05-26 17:49:51
- kind: validation
- status: open
- repository: /userdata/alldemo
- summary: CAP_DEHAZE and DCP_FAST_DEHAZE bind-chain display matched the old single-pane layout but rendered a green half-frame, so the stable CPU live page was kept and reshaped instead.

## Symptom

The CAP/DCP bind-chain screenshots showed a strong green lower band and incorrect color even though capture status was ok.

## Reproduction

```bash
./scripts/capture_page_screens.sh recordings/visual_check/check_cap_dcp_bind_20260526_1835 CAP_DEHAZE DCP_FAST_DEHAZE
```

## Evidence

- recordings/visual_check/page_compare_cap_dcp_bind_20260526_1835/contact.jpg showed green half-frame output on both CAP_DEHAZE and DCP_FAST_DEHAZE.

## Affected Files

- src/pages/page_cap_dehaze_offline.c

## Root Cause

suspected cause: the live bind chain has an RGB/NV12 format or stride mismatch between dehaze, resize, OSD, and VO; not proven in this pass.

## Fix Or Workaround

Workaround used: keep run_dehaze_live_cpu_page() for CAP_DEHAZE and DCP_FAST_DEHAZE, but draw a single 1080x608 pass/enhance view to match the original display structure.

## Recurrence Prevention

Before switching CAP/DCP back to run_dehaze_live_page(), capture CAP_DEHAZE and DCP_FAST_DEHAZE and inspect the PNG for green bands or color-plane corruption, not just summary.tsv status.

## Related

- None
