# EDOF_CL customer page stalls shutdown with live OpenCL and per-frame redraw

metadata:
- date: 2026-05-18 17:36:31
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: EDOF_CL default page lacked runtime logs and short-run cleanup evidence because live OpenCL and repeated CPU redraw made shutdown unreliable.

## Symptom

Baseline EDOF_CL runs could generate an old-style capture but did not print pool, pipeline, or buffer-manager cleanup before timeout/alarm exit.

## Reproduction

```bash
timeout -s INT -k 5s 12s stdbuf -oL -eL ./scripts/run_alldemo.sh --only EDOF_CL > /tmp/alldemo_edof_cl_srs001_baseline.log 2>&1
```

## Evidence

- Fixed run /tmp/alldemo_edof_cl_srs001_direct_16s.log reached EDOF_CL frames=420, generated edof_cl_vo_000150.bmp and edof_cl_vo_000300.bmp, and printed pool/pipeline/buffer-manager cleanup.

## Affected Files

- src/alldemo.c
- README.md
- assets/effect_manifest.json

## Root Cause

The customer page started the live EDOF_CL OpenCL module by default and also redrew the three large comparison images every display frame; the slow processing/redraw path could keep the main loop or worker cleanup from reaching normal shutdown in short acceptance runs.

## Fix Or Workaround

Default the customer page to stable reference-fusion samples, require ALLDEMO_EDOF_CL_LIVE=1 for explicit live-module testing, cache the full EDOF_CL NV12 page by sample index, and log frames, sample index, updates, mode, cache state, CPU/GPU/RGA.

## Recurrence Prevention

For static sample comparison pages, cache page renders by sample key and do not enable slow live GPU workers in customer mode unless their cleanup is separately proven.

## Related

- SRS-EDOF-CL-CUSTOMER-PAGE-001
