# WBC H264 recorder stops at 3000 frames without license

metadata:
- date: 2026-05-19 08:00:45
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: VO_WBC file recorder hit the default unlicensed 3000-frame guard and produced only about 100 seconds of H.264.

## Symptom

Recorder log contained: Module VO_WBC_0: frame limit reached (3000 >= 3000); recorded packets=3000.

## Reproduction

```bash
/userdata/rktohi/build/demo_vo_wbc_record /userdata/alldemo/recordings/alldemo_wbc_20260519_074018.h264 170 1024 1920 30 8000000 0
```

## Evidence

- /tmp/alldemo_wbc_record_20260519.log: frame limit reached at 3000 packets
- /tmp/alldemo_wbc_record_final_20260519.log: recorded packets=5475 with license loaded

## Affected Files

- tools/wbc_h264_record.c
- CMakeLists.txt
- /userdata/rktohi/demo/vo_wbc/demo_vo_wbc_record.c
- /userdata/rktohi/src/common/pipeline/module_base.c

## Root Cause

The existing demo_vo_wbc_record path did not call MEDIA_SYS_SetLicense('/root/licence.dat'), so module_base kept the default unlicensed frame limit.

## Fix Or Workaround

Added alldemo build/wbc_h264_record, which calls MEDIA_SYS_SetLicense('/root/licence.dat') before creating WBC and VENC modules.

## Recurrence Prevention

For long WBC/VENC captures, verify the recorder log contains [LICENSE] Loaded mask and that recorded packets exceed 3000 when duration requires it.

## Related

- None
