# 4K camera RGA/RESIZE_RGA crop alignment failures

metadata:
- date: 2026-05-19 11:57:00
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: RGA COPY could not scale 3840x2160 to 640x640 and RESIZE_RGA rejected odd NV12 crop rectangles after 4K scaling.

## Symptom

RGA logged 'imcopy cannot support scale' for COPY; RESIZE_RGA logged 'err yuv not align to 2' and ret=-22 for crop rectangles such as 3120x1755.

## Reproduction

```bash
timeout --preserve-status 8s ./scripts/run_alldemo.sh --only RGA; timeout --preserve-status 8s ./scripts/run_alldemo.sh --only RESIZE_RGA
```

## Evidence

- RGA before fix: Invalid parameters: imcopy cannot support scale, src[w,h]=[3840,2160], dst[w,h]=[640,640].
- RESIZE_RGA before fix: err yuv not align to 2; rect[0,0,3120,1755,3840,2160,...].
- After fix: RGA vi_frames/rga_frames grew to 214; RESIZE_RGA resize_frames grew in each dynamic crop segment and saved resize_rga_vo_000150.bmp.

## Affected Files

- src/alldemo.c

## Root Cause

4K input changed source dimensions while COPY kept a scaling output; resize crop parameters scaled from 640 baseline to 2160 height could become odd, which NV12/RGA rejects.

## Fix Or Workaround

Use RESIZE algo for scaled COPY preview, clamp RGA crop to min(width,height), and force RESIZE_RGA source x/y/w/h to even values within input bounds.

## Recurrence Prevention

When moving camera pages to 3840x2160, validate module frame counters and inspect RGA source rectangles for even x/y/w/h before accepting runtime output.

## Related

- PRODUCT-CUSTOMER-DEMO-ITERATION-003
