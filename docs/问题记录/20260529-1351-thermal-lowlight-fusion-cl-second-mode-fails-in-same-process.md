# THERMAL_LOWLIGHT_FUSION_CL second mode fails in same process

metadata:
- date: 2026-05-29 13:51:08
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: Running gray and black-red thermal lowlight fusion modes sequentially in one process hits CL image creation error -34; isolated pair-mode processes work.

## Symptom

The second generated mode failed with: THERMAL_LOWLIGHT_FUSION_CL: create thermal Y CL image failed -34; THERMAL_LOWLIGHT_FUSION_CL page: get output failed.

## Reproduction

```bash
./scripts/run_alldemo.sh --only THERMAL_LOWLIGHT_FUSION_CL
```

## Evidence

- None

## Affected Files

- src/pages/page_thermal_lowlight_fusion_cl.c
- /userdata/rktohi/demo/thermal_lowlight_fusion_cl/demo_thermal_lowlight_fusion_cl_smoke.c

## Root Cause

The module or Mali/EGL/OpenCL path does not cleanly support a second mode/group in the same process after the first mode has initialized and torn down CL/EGL resources. The upstream demo --pair path reproduces the failure, while separate --pair-mode processes succeed.

## Fix Or Workaround

The page now generates each sample/mode by invoking the rktohi demo with --pair-mode in a separate process, then loads the generated JPG outputs into the display cache.

## Recurrence Prevention

For THERMAL_LOWLIGHT_FUSION_CL demos, use one process per mode unless the module is fixed to support repeated same-process mode creation; verify by running the upstream --pair command before embedding repeated mode runs.

## Related

- None
