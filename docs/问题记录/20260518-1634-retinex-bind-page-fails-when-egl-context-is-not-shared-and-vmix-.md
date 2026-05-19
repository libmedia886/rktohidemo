# RETINEX bind page fails when EGL context is not shared and VMIX reimports output

metadata:
- date: 2026-05-18 16:34:13
- kind: runtime
- status: resolved
- repository: /userdata/alldemo
- summary: RETINEX --only initially logged CL image import -34 and held VPSS/VMIX pools; page now uses RETINEX OpenCL plus VMIX_RGA display composition.

## Symptom

Logs showed repeated 'RETINEX: 创建输入 Y 平面 CL 图像失败 -34', retinex_frames=0, and pool destroy rejection for pool_id=14 and pool_id=7.

## Reproduction

```bash
timeout -s INT -k 3s 9s stdbuf -oL -eL ./scripts/run_alldemo.sh --only RETINEX
```

## Evidence

- Failing log: /tmp/alldemo_retinex_srs001_baseline.log had repeated RETINEX input Y CL image -34 and pool destroy rejections.
- Passing logs: /tmp/alldemo_retinex_srs001_after_context_rga.log and /tmp/alldemo_retinex_srs001_ctrlc.log show RETINEX frames growing, grep count 0, VO captures, and clean pool destruction.

## Affected Files

- src/alldemo.c
- /userdata/rktohi/src/module/retinex/retinex.c
- lib/libmedia.a

## Root Cause

RETINEX created a default OpenCL context instead of an EGL-sharing context and did not bind its EGL context in the processing thread; after that, the customer display path still needed to avoid feeding GPU-produced dma-bufs into VMIX OpenCL.

## Fix Or Workaround

Create RETINEX OpenCL context with CL_GL_CONTEXT_KHR and CL_EGL_DISPLAY_KHR, bind EGL context in the processing thread, unbind the init thread context, switch page composition to VMIX_RGA, rebuild rktohi libmedia, and sync libmedia.a into alldemo.

## Recurrence Prevention

For OpenCL/EGL modules in bind pages, verify shared-context creation and processing-thread eglMakeCurrent before debugging formats; use VMIX_RGA or direct display when VMIX OpenCL reimport risks -34.

## Related

- SRS-RETINEX-CUSTOMER-PAGE-001
