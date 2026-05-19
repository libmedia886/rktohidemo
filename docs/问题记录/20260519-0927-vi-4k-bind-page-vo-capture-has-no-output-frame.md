# VI 4K bind page VO capture has no output frame

metadata:
- date: 2026-05-19 09:27:11
- kind: validation
- status: open
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

## Fix Or Workaround

No code workaround applied yet; keep VI 4K path enabled and use live screen confirmation. Next diagnostic step is to add a capture source after RESIZE_RGA or OSD that does not consume the display-bound queue.

## Recurrence Prevention

When changing bind paths, validate both live frame counters and capture availability; do not assume MEDIA_VO_GetFrame works for every bound display path.

## Related

- SRS-VI-CUSTOMER-PAGE-001
