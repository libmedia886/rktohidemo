# VO_WBC attr field drift after rktohi sync

metadata:
- date: 2026-05-18 13:30:04
- kind: compile
- status: fixed
- repository: /userdata/alldemo
- summary: run_alldemo synced a newer media_api.h where MEDIA_VO_WBC_ATTR no longer has connector_id/crtc_id, causing alldemo.c to fail compilation.

## Symptom

src/alldemo.c:4779:9: error: 'MEDIA_VO_WBC_ATTR' has no member named 'connector_id'; src/alldemo.c:4780:9: error: 'MEDIA_VO_WBC_ATTR' has no member named 'crtc_id'

## Reproduction

```bash
./scripts/run_alldemo.sh --asset-check
```

## Evidence

- `./scripts/run_alldemo.sh --asset-check` synced `include/media_api.h`, `lib/libmedia.a`, and `lib/libmedia.so`, then rebuilt and failed on the removed fields.
- Current `include/media_api.h` defines `MEDIA_VO_WBC_ATTR` with `device`, `target`, `width`, `height`, `stride`, `fps`, `format`, `pool_id`, and `output_depth`.

## Affected Files

- src/alldemo.c
- include/media_api.h
- scripts/run_alldemo.sh

## Root Cause

The current rktohi VO_WBC public attr selects writeback connector and active CRTC internally from device/target; Probe still returns connector/crtc only for diagnostics.

## Fix Or Workaround

Removed attr.connector_id and attr.crtc_id assignments from setup_live_wbc while keeping MEDIA_VO_WBC_Probe for display diagnostics.

## Recurrence Prevention

After run_alldemo syncs include/media_api.h from rktohi, rebuild immediately and compare changed public structs against alldemo call sites before assuming copied libraries are compatible.

## Related

- None
