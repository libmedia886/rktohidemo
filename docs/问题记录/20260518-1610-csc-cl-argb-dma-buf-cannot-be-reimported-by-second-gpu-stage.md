# CSC_CL ARGB dma-buf cannot be reimported by second GPU stage

metadata:
- date: 2026-05-18 16:10:03
- kind: runtime
- status: resolved
- repository: /userdata/alldemo
- summary: CSC_CL output dma-buf failed with -34 when reimported by another CSC_CL or VMIX OpenCL context; page now displays through OSD/VO ARGB path.

## Symptom

Two-stage CSC_CL and CSC_CL->VMIX paths logged clCreateFromEGLImageKHR failures such as 'CSC_CL: 创建 input 主 CL 图像失败 -34' and 'VMIX CL: create input packed CL image failed -34'.

## Reproduction

```bash
timeout -s INT -k 3s 9s stdbuf -oL -eL ./scripts/run_alldemo.sh --only CSC_CL
```

## Evidence

- Failing logs: /tmp/alldemo_csc_cl_srs001_after_fallback.log and /tmp/alldemo_csc_cl_srs001_single_cl.log
- Passing logs: /tmp/alldemo_csc_cl_srs001_final2.log and /tmp/alldemo_csc_cl_srs001_ctrlc.log, generated vo_captures/csc_cl_vo_000150.bmp

## Affected Files

- src/alldemo.c
- /userdata/rktohi/src/module/csc_cl/csc_cl.c
- /userdata/rktohi/src/module/osd/osd.c
- /userdata/rktohi/src/common/format/format_utils.c

## Root Cause

The board driver accepts CSC_CL NV12->ARGB and ARGB direct VO display, but rejects importing the CSC_CL-produced ARGB dma-buf into a second OpenCL/EGL consumer context. The accepted page path avoids that second GPU import.

## Fix Or Workaround

Use VI -> CSC_CL(NV12->ARGB8888) -> OSD(ARGB8888) -> VO(ARGB8888), and add ARGB8888 support to the OSD module/caps in rktohi.

## Recurrence Prevention

Before chaining GPU/EGL modules through the same dma-buf, run a short --only page proof and check for -34 import failures; prefer VO/OSD direct ARGB display for CSC_CL customer page.

## Related

- SRS-CSC-CL-CUSTOMER-PAGE-001
