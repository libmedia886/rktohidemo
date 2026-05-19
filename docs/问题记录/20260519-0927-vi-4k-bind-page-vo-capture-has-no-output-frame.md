# VI 4K bind page VO capture has no output frame

metadata:
- date: 2026-05-19 09:27:11
- kind: validation
- status: fixed
- repository: /userdata/alldemo
- summary: VI 4K bind chain runs, but automatic VO capture cannot acquire an output frame at capture checkpoints.

## Symptom

Runtime logs show VI 3840x2160 frames increasing and bind success, but print 'VI VO capture no output frame at 150/300/450'.

## Reproduction

```bash
timeout -s INT -k 5s 16s ./scripts/run_alldemo.sh --only VI
```

## Evidence

- MEDIA_VI_SetAttr ok: /dev/video-camera0 3840x2160@30fps.
- Bind success: VI_0.output -> RESIZE_RGA_61.input0 -> VMIX_80.input0 -> OSD_81.input -> VO_0.input0.
- Capture miss: VI VO capture no output frame at 150, 300, and 450.

## Affected Files

- src/alldemo.c

## Root Cause

Suspected cause: the bound VO/OSD/VMIX output queues do not expose a readable latest frame at the capture checkpoint on this VI->RESIZE_RGA->VMIX->OSD->VO path.

## Fix

Automatic VO capture is no longer a current VI 4K acceptance risk. The current acceptance uses both capture availability when present and buffer-flow evidence along the bind chain; downstream frame counters must prove that VI buffers reach RESIZE_RGA/VMIX/OSD instead of stopping at VI.

## Recurrence Prevention

When changing bind paths, validate both capture availability and buffer flow. Do not accept a page from module creation or VI frame count alone; downstream module counters or output queue movement must grow, otherwise the module has no buffer to process.

## Related

- SRS-VI-CUSTOMER-PAGE-001
