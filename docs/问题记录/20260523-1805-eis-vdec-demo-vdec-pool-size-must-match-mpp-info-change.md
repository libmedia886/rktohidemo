# EIS VDEC demo VDEC pool size must match MPP info_change

metadata:
- date: 2026-05-23 18:05:59
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: VDEC H264 elementary stream output pool sized as plain NV12 was too small for MPP aligned decode buffers.

## Symptom

VDEC info_change: output pool buffer too small, need=471040 have=345600

## Reproduction

```bash
./build/eis_vdec_vpss_vmix_osd_vo --no-vo --frames 60
```

## Evidence

- VDEC info_change: 640x360 stride=640x368 buf_size=471040 pool_size=345600

## Affected Files

- src/alldemo.c

## Root Cause

MPP reports 640x360 decode buffers as 640x368 with a 471040-byte external buffer requirement; sizing VDEC pool as width*height*3/2 under-allocates.

## Fix Or Workaround

Use stride*align(height,16)*2 for the VDEC output pool while keeping downstream VPSS/EIS pools at NV12 frame size.

## Recurrence Prevention

For VDEC tests, check the info_change buf_size or allocate VDEC output pools with the MPP-aligned size before binding decoded frames downstream.

## Related

- None
