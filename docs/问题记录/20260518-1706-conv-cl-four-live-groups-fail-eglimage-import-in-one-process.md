# CONV_CL four live groups fail EGLImage import in one process

metadata:
- date: 2026-05-18 17:06:24
- kind: runtime
- status: resolved
- repository: /userdata/alldemo
- summary: CONV_CL --only initially used four live CONV_CL groups; only SHARPEN advanced while EDGE/EMBOSS/BLUR hit clCreateFromEGLImageKHR -34. The page now uses one CONV_CL group sequent...

## Symptom

The four-branch bind page repeatedly logged CONV_CL: clCreateFromEGLImageKHR 失败 ret=-34; SHARPEN frames increased but EDGE/EMBOSS/BLUR stayed at 0.

## Reproduction

```bash
timeout -s INT -k 3s 9s stdbuf -oL -eL ./scripts/run_alldemo.sh --only CONV_CL
```

## Evidence

- /tmp/alldemo_conv_cl_srs001_baseline.log: repeated clCreateFromEGLImageKHR ret=-34 and CONV_CL vi_frames=0.
- /tmp/alldemo_conv_cl_srs001_vmix_rga.log: display changed to VMIX_RGA but only SHARPEN advanced; other branches stayed 0.
- /tmp/alldemo_conv_cl_srs001_final.log: SHARPEN/EDGE/EMBOSS/BLUR reached 209, clCreateFromEGLImageKHR count 0, set_param spam count 0, cleanup had no pool reject.

## Affected Files

- src/alldemo.c
- README.md
- assets/effect_manifest.json
- /userdata/rktohi/src/module/conv_cl/conv_cl.c

## Root Cause

The failing boundary is the four concurrent CONV_CL groups importing RGBA dma-bufs in the same process on this EGL/OpenCL stack. Standalone CONV_CL and one CONV_CL group processing the four kernels sequentially work; treat the original design as a board-specific multi-instance EGLImage import limitation.

## Fix Or Workaround

Use a single MEDIA_CONV_CL group for the customer page, convert VI NV12 to RGBA staging, sequentially load SHARPEN/EDGE/EMBOSS/BLUR tables, compose the four real outputs into one page, suppress table-update log spam behind CONV_CL_LOG_TABLE_UPDATE, and skip early CAMERA_POOL destroy for this CPU-send page.

## Recurrence Prevention

Before adding multiple OpenCL/EGL instances to one live page, run a short --only probe and check every branch count, clCreateFromEGLImageKHR, VO capture, and Ctrl+C pool cleanup. Prefer one proven OpenCL group with explicit page composition when multi-instance imports fail.

## Related

- SRS-CONV-CL-CUSTOMER-PAGE-001
