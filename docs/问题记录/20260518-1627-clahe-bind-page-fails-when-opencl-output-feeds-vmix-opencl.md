# CLAHE bind page fails when OpenCL output feeds VMIX OpenCL

metadata:
- date: 2026-05-18 16:27:50
- kind: runtime
- status: resolved
- repository: /userdata/alldemo
- summary: CLAHE --only initially logged CL image import -34 and later VMIX OpenCL -34; page now uses CLAHE OpenCL plus VMIX_RGA display composition.

## Symptom

Logs showed 'CLAHE: create input Y CL image failed ret=-34' / 'create output Y CL image failed ret=-34', then after context fix 'VMIX CL: create input Y CL image failed -34' with clahe_out_frames=0.

## Reproduction

```bash
timeout -s INT -k 3s 9s stdbuf -oL -eL ./scripts/run_alldemo.sh --only CLAHE
```

## Evidence

- Failing log: /tmp/alldemo_clahe_srs001.log had CLAHE create input/output Y CL image ret=-34.
- Intermediate log: /tmp/alldemo_clahe_srs001_private_pool.log moved the failure to VMIX CL create input Y CL image failed -34.
- Passing logs: /tmp/alldemo_clahe_srs001_final.log and /tmp/alldemo_clahe_srs001_ctrlc.log show CLAHE frames growing, grep count 0, and VO captures.

## Affected Files

- src/alldemo.c
- /userdata/rktohi/src/module/clahe/clahe.c
- lib/libmedia.a

## Root Cause

CLAHE needed an EGL-sharing OpenCL context bound on its processing thread, and the board rejected feeding VPSS/CLAHE-produced dma-bufs into a second VMIX OpenCL consumer. The public CLAHE frame counter also was not incremented on the bind-thread success path.

## Fix Or Workaround

Create CLAHE before display VMIX, give CLAHE its own output pool, switch the compare compositor to VMIX_RGA, increment CLAHE module->frame_count after successful processing, rebuild rktohi libmedia, and sync libmedia.a into alldemo.

## Recurrence Prevention

For pages that chain GPU/EGL producers into another display stage, check for -34 import failures first; prefer VMIX_RGA or direct OSD/VO for display composition, and verify module frame counters in bind-thread modules.

## Related

- SRS-CLAHE-CUSTOMER-PAGE-001
