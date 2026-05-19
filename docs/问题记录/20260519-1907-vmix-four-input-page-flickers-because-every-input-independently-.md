# VMIX four-input page flickers because every input independently triggers composition

metadata:
- date: 2026-05-19 19:07:47
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: Four-input VMIX page flickered because VMIX cleared and emitted a frame for each input-port arrival instead of using one cadence frame.

## Symptom

In /userdata/alldemo --only VMIX, any quadrant could flicker briefly; input0 flickered most often. Increasing buffers, disabling VMIX capture BMP, and freezing VPSS dynamic crop reduced variables but did not address the root output cadence issue.

## Reproduction

```bash
timeout -s INT -k 3s 40s env -u DISPLAY MEDIA_VMIX_EGL_BACKEND=gbm ./scripts/run_alldemo.sh --only VMIX
```

## Evidence

- Before cadence fix, VMIX frames ran faster than VI frames, e.g. vi_frames=691 vmix_frames=719.
- After cadence fix, 40s --only VMIX showed vi_frames and vmix_frames approximately 1:1, e.g. vi_frames=1142 vmix_frames=1142, with no VMIX GPU failure, no VO capture, and no Buffer queue full.

## Affected Files

- /userdata/rktohi/src/module/vmix/vmix.c
- /userdata/alldemo/src/alldemo.c
- /userdata/alldemo/lib/libmedia.a

## Root Cause

VMIX thread polled input0..input3 and called process for whichever input had a frame. Each process cleared the output and recomposited immediately, producing intermediate frames such as input0-new with other channels still old. Because input0 was polled first, it was the most visible trigger.

## Fix Or Workaround

Changed src/module/vmix/vmix.c so multi-input VMIX uses primary/input0 as the composition cadence, waits briefly for other input queues, and emits one composed output per cadence frame. In alldemo, VMIX page auto BMP capture was disabled, VPSS dynamic crop/flip was disabled for the VMIX page, and the VPSS quad output pool was increased from 16 to 24 buffers.

## Recurrence Prevention

For multi-input VMIX flicker, first check whether vmix_frames runs faster than vi_frames. If yes, inspect VMIX input polling/output cadence before blaming last-frame retention, BMP capture, or pool size. Validate with --only VMIX and expect vmix_frames approximately equal to vi_frames.

## Related

- None
