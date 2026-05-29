# THERMAL_LOWLIGHT_FUSION_CL page used invalid pool ids

metadata:
- date: 2026-05-29 13:51:08
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: The live fusion page initially used pool ids 62-64, but rktohi buffer manager only accepts ids 0..15.

## Symptom

Startup failed before display with: pool_id或count无效; THERMAL_LOWLIGHT_FUSION_CL page: pool create failed.

## Reproduction

```bash
./scripts/run_alldemo.sh --only THERMAL_LOWLIGHT_FUSION_CL
```

## Evidence

- None

## Affected Files

- src/pages/page_thermal_lowlight_fusion_cl.c
- /userdata/rktohi/src/common/buffer/buffer_manager.c

## Root Cause

rktohi/src/common/buffer/buffer_manager.c defines MAX_POOLS 16, so pool ids must be below 16. The page used ids copied from a local high-id namespace instead of the demo-validated 10/11/12 range.

## Fix Or Workaround

Changed the page to use the demo-compatible pool ids 10, 11, and 12 for thermal input, lowlight input, and fusion output generation.

## Recurrence Prevention

Before adding page-local media pools, check MAX_POOLS and reuse demo-proven pool ids below 16 when running a single isolated module.

## Related

- None
