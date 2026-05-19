# TRANSFORM four-way 4K demo stalls perspective mode

metadata:
- date: 2026-05-19 19:18:44
- kind: runtime
- status: resolved
- repository: /userdata/alldemo
- summary: TRANSFORM --only previously ran three 4K transform branches in parallel, saturating GPU and starving the final PERSPECTIVE branch.

## Symptom

The TRANSFORM test page was visibly choppy; the final PERSPECTIVE tile barely moved. Probe showed vi_frames=230 while PERSPECTIVE=2/2, with repeated VPSS output pool shortage and VMIX queue full logs.

## Reproduction

```bash
timeout -s INT -k 5s 12s stdbuf -oL -eL ./scripts/run_alldemo.sh --only TRANSFORM
```

## Evidence

- Before: TRANSFORM vi_frames=230 RAW=206/190 UNDISTORT=117/117 ROTATE ZOOM=41/41 PERSPECTIVE=2/2 cpu=57% gpu=98% rga=24%.
- After: TRANSFORM vi_frames=253 mode=PERSPECTIVE vpss=249 transform=243 resize=243 cpu=51% gpu=96% rga=14%.

## Affected Files

- src/alldemo.c

## Root Cause

The demo topology fed four full-resolution VPSS outputs into one raw resize plus three independent 3840x2160 TRANSFORM groups. GPU usage reached about 98%, downstream queues backed up, and the last transform branch was starved.

## Fix Or Workaround

Changed the page to one 4K TRANSFORM branch and switch its LUT once per second across RAW, UNDISTORT, ROTATE ZOOM, and PERSPECTIVE, then resize to the single large VMIX input.

## Recurrence Prevention

For customer-facing TRANSFORM demos, keep mode comparison time-multiplexed unless the hardware can sustain multiple 4K LUT branches; verify logs include all four modes advancing before accepting the page.

## Related

- None
