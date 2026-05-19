# STEREO_3D 4K input has no merged frame flow

metadata:
- date: 2026-05-19 11:27:29
- kind: validation
- status: open
- repository: /userdata/alldemo
- summary: STEREO_3D can bind VI 3840x2160 through VPSS 640x640 outputs, but merged_frames stays zero with repeated EGL/OpenCL image import failures.

## Symptom

Runtime showed VI 3840x2160 and VPSS output0/output1 640x640 bound into STEREO_3D, but merged_frames=0 and logs repeatedly printed STEREO_3D: create input0 Y CL image failed -34.

## Reproduction

```bash
timeout -s INT -k 5s 8s ./scripts/run_alldemo.sh --only STEREO_3D
```

## Evidence

- MEDIA_VI_SetAttr: ok dev=0 device=/dev/video-camera0 3840x2160 fps=30
- STEREO_3D vi_frames=181 merged_frames=0 rotate=90
- STEREO_3D: create input0 Y CL image failed -34

## Affected Files

- src/alldemo.c
- docs/AI团队/demo验收/ACCEPT-CUSTOMER-DEMO-4K-CAMERA-COMPARE-20260519-001.yaml

## Root Cause

Suspected cause: STEREO_3D OpenCL/EGL import cannot consume the VPSS output dma-buf in this path, so downstream buffer flow stops at STEREO_3D.

## Fix Or Workaround

Pending: fix STEREO_3D import path or switch display composition to a compatible intermediate before re-enabling 4K input for this page.

## Recurrence Prevention

For bind pages, verify every downstream branch counter grows; do not accept a page when bind succeeds but merged/output frame count remains zero.

## Related

- PRODUCT-CUSTOMER-DEMO-ITERATION-003
