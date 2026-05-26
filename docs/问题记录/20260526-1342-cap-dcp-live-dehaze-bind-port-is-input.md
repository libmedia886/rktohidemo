# CAP/DCP live dehaze bind port is input

metadata:
- date: 2026-05-26 13:42:06
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: CAP_DEHAZE and DCP_FAST_DEHAZE live pages must bind CSC_RGA output to dehaze port input, not input0.

## Symptom

Bind failed when CSC_RGA output was connected to CAP_DEHAZE/DCP_FAST_DEHAZE input0.

## Reproduction

```bash
timeout -s INT -k 3s 8s ./scripts/run_alldemo.sh --only CAP_DEHAZE
```

## Evidence

- Module CAP_DEHAZE_66: added input port 'input'
- Module DCP_FAST_DEHAZE_67: added input port 'input'

## Affected Files

- src/pages/page_cap_dehaze_offline.c
- src/pages/page_ops.c

## Root Cause

The dehaze modules expose their input port as input, while several image-processing modules use input0; copying the RGA-style port name made the live chain incompatible.

## Fix Or Workaround

Bind and unbind CSC_RGA.output0 to CAP_DEHAZE/DCP_FAST_DEHAZE input in src/pages/page_cap_dehaze_offline.c.

## Recurrence Prevention

Before wiring a standalone page, confirm actual module port names from the create log or media_api contract instead of assuming input0.

## Related

- None
