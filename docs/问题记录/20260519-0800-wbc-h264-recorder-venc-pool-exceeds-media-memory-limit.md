# WBC H264 recorder VENC pool exceeds media memory limit

metadata:
- date: 2026-05-19 08:00:55
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: A first local WBC recorder build requested too many VENC packet buffers and failed pool creation.

## Symptom

Recorder log contained: pool总内存超过上限: requested=268435456 current=23592960 limit=268435456; MEDIA_POOL_Create VENC failed.

## Reproduction

```bash
./build/wbc_h264_record /userdata/alldemo/recordings/alldemo_wbc_full_20260519_080000.h264 190 1024 1920 30 8000000
```

## Evidence

- /tmp/alldemo_wbc_record_full_20260519.log: MEDIA_POOL_Create VENC failed
- /tmp/wbc_h264_record_cleanup_test.log: Pool销毁完成 for id=15 and id=14

## Affected Files

- tools/wbc_h264_record.c
- CMakeLists.txt

## Root Cause

VENC_POOL_COUNT was set to 32 with 8 MiB buffers, which requested 256 MiB and exceeded the global pool limit once existing pools were allocated.

## Fix Or Workaround

Reduced VENC_POOL_COUNT to 8 and added a short VENC packet drain after stopping WBC so the VENC output pool is destroyed cleanly.

## Recurrence Prevention

Keep VENC packet pool count aligned with existing demos unless bitrate/resolution requires more, and confirm recorder cleanup logs include Pool销毁完成 for both id=15 and id=14.

## Related

- None
