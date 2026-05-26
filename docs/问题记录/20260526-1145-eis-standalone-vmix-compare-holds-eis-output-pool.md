# EIS standalone VMIX compare holds EIS output pool

metadata:
- date: 2026-05-26 11:45:29
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: EIS RAW/EIS VMIX compare can hold one EIS output buffer if pool 3 is explicitly destroyed before MEDIA_SYS_Exit.

## Symptom

EIS counts and stats advanced, but shutdown reported pool_id=3 still had 1 buffer not returned.

## Reproduction

```bash
timeout -s INT -k 3s 8s ./scripts/run_alldemo.sh --only EIS
```

## Evidence

- EIS vdec=58 vpss=58 eis=58 vmix=58 vo=58
- pool_id=3 still has 1 buffer not returned; refused destroy

## Affected Files

- src/pages/page_eis.c

## Root Cause

The RAW/EIS VMIX compare path can leave one EIS output buffer owned by the downstream VMIX input side during shutdown.

## Fix Or Workaround

Restored the RAW/EIS VMIX compare page and kept explicit unbind/drain cleanup, but no longer explicitly destroys pool 3. MEDIA_SYS_Exit reclaims that pool after VMIX/EIS teardown, matching the CAP/DCP live VI cleanup pattern.

## Recurrence Prevention

When changing EIS compare cleanup, run `./scripts/capture_page_screens.sh <out> EIS` and grep for `拒绝销毁`; do not reintroduce explicit pool 3 destruction unless downstream VMIX input release is proven clean.

## Related

- SRS-ALLDEMO-ARCHITECTURE-SPLIT-038
