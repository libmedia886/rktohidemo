# EIS VDEC demo must drain outputs before MEDIA_SYS_Exit

metadata:
- date: 2026-05-23 18:05:59
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: The demo could segfault inside MPP when MEDIA_SYS_Exit ran immediately after packet submission.

## Symptom

Segmentation fault after VDEC info_change; gdb showed mpp_list::list_size(this=0x0) from mpi_decode_get_frame.

## Reproduction

```bash
./build/eis_vdec_vpss_vmix_osd_vo --no-vo --frames 10
```

## Evidence

- gdb: Thread received SIGSEGV at mpp_list::list_size -> Mpp::get_frame -> mpi_decode_get_frame

## Affected Files

- src/alldemo.c

## Root Cause

The test exited and destroyed the pipeline while the VDEC decode thread was still processing info_change/output frames.

## Fix Or Workaround

Drain OSD output or wait for module frame counts, print summary, and stop VDEC/VPSS/EIS/VMIX/OSD/VO before MEDIA_SYS_Exit.

## Recurrence Prevention

For threaded media pipeline tests, do not call MEDIA_SYS_Exit immediately after input submission; wait for downstream frame counts and stop modules in source-to-sink order first.

## Related

- None
