# CLAHE bind path uses output port not output0

metadata:
- date: 2026-05-26 13:30:32
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: CLAHE standalone VI bind failed because the module exposes output, not output0.

## Symptom

Runtime setup failed with: pipeline_connect: Source module CLAHE_69 has no output port

## Reproduction

```bash
timeout -s INT -k 3s 8s ./scripts/run_alldemo.sh --only CLAHE
```

## Evidence

- MEDIA log showed Module CLAHE_69: added output port 'output' before the failed bind.

## Affected Files

- src/pages/page_clahe.c

## Root Cause

The standalone migration used the common output0 port name, but CLAHE's media module registers its source port as output.

## Fix Or Workaround

Changed the CLAHE -> RESIZE_RGA bind and cleanup unbind to use CLAHE_69.output.

## Recurrence Prevention

Before binding a migrated module, check its module_add_input_port_advanced log or legacy bind_first_match port list; not all modules expose output0.

## Related

- None
