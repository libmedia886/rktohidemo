# RESIZE_RGA bind input width inferred from crop

metadata:
- date: 2026-05-18 14:14:31
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: RESIZE_RGA --only page exited immediately because its input port was configured as the crop size instead of the VI frame size.

## Symptom

Default rotation reached RESIZE_RGA, then the RESIZE_RGA child exited after a VI -> RESIZE_RGA bind failure.

## Reproduction

```bash
timeout -s INT 12s ./build/alldemo --only RESIZE_RGA
```

## Evidence

- port_check_compatibility: Width mismatch; Source width: 640; Destination width: 520
- bind failed: VI -> RESIZE_RGA
- /tmp/alldemo_resize_rga_fix2.log shows only=RESIZE_RGA ran until timeout with repeated RESIZE_RGA frame counters after the fix.

## Affected Files

- src/alldemo.c
- scripts/run_alldemo.sh

## Root Cause

fill_resize_attr() left input_width/input_height at zero, so the resize module inferred the input port dimensions from src_x + crop_w and src_y + crop_h. At frame 0 that made input0 520x520 while VI outputs 640x640.

## Fix Or Workaround

Set MEDIA_RESIZE_RGA_ATTR.input_width/input_height to CAM_W/CAM_H in fill_resize_attr(), keeping src_width/src_height as the moving crop window.

## Recurrence Prevention

For bind-mode RESIZE_RGA pages, verify the module input port size matches the upstream full frame size; do not rely on crop-derived default input dimensions.

## Related

- None
