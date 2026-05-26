# MEDIA_VI_GetFrame reads wrong VI output port

metadata:
- date: 2026-05-26 16:19:08
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: CAP_DEHAZE and DCP_FAST_DEHAZE CPU live pages showed black panes because MEDIA_VI_GetFrame read VI output0 while the VI module exposes output.

## Symptom

CAP_DEHAZE/DCP_FAST_DEHAZE screenshots had OSD but VI and dehaze frame counters stayed at 0; run logs showed VI started and Module VI_0 added output port 'output'.

## Reproduction

```bash
./scripts/capture_page_screens.sh recordings/visual_check/final_fix_cap_cpu_* CAP_DEHAZE DCP_FAST_DEHAZE
```

## Evidence

- Before fix: CAP_DEHAZE vi_frames=0 cap_frames=0 and black VI/CAP panes.
- Source inspection: MEDIA_VI_GetFrame used media_get_frame_from_module(mod, 'output0', ...), while vi.c registers module_add_output_port(module, 'output', ...).
- After fix: full_after_cap_fix_20260526_161215 CAP_DEHAZE vi_frames=89 cap_frames=89; DCP_FAST_DEHAZE vi_frames=89 dcp_frames=89.

## Affected Files

- /userdata/rktohi/src/media_api.c
- src/pages/page_cap_dehaze_offline.c
- lib/libmedia.a
- lib/libmedia.so
- include/media_api.h

## Root Cause

MEDIA_VI_GetFrame used the wrong VI output port name. The current VI module exposes 'output', so manual GetFrame never received frames.

## Fix Or Workaround

Changed /userdata/rktohi/src/media_api.c to read VI port 'output', rebuilt rktohi, synced libmedia.a/libmedia.so/media_api.h into /userdata/alldemo, and kept CAP/DCP live pages on real VI CPU-composed comparison.

## Recurrence Prevention

For VI manual capture pages, verify the actual registered VI port name in logs and require frame counters to grow, not just VI startup.

## Related

- ALLDEMO visual full check 20260526
