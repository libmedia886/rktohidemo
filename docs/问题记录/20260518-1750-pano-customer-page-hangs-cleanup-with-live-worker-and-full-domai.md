# PANO customer page hangs cleanup with live worker and full-domain output

metadata:
- date: 2026-05-18 17:50:59
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: PANO live customer page built a huge full-domain output and did not reach capture or cleanup in the acceptance window.

## Symptom

Baseline PANO built a 561MB LUT for 8378x4190 output, logged active-dispatch, then produced no VO capture and no pool/pipeline cleanup before timeout.

## Reproduction

```bash
timeout -s INT -k 20s 35s stdbuf -oL -eL ./scripts/run_alldemo.sh --only PANO > /tmp/alldemo_pano_srs001_baseline.log 2>&1
```

## Evidence

- Fixed default run /tmp/alldemo_pano_srs001_direct_reference.log reached PANO frames=480, generated pano_vo_000150/000300/000450.bmp, and printed pool/pipeline/buffer-manager cleanup.
- Live preview run /tmp/alldemo_pano_srs001_fix2_cleanup.log reached frames but still did not print cleanup with a 30s kill window.

## Affected Files

- src/alldemo.c
- README.md
- assets/effect_manifest.json

## Root Cause

The customer page used the live PANO worker by default with an 8378x4190 output; even after reducing output to 1920x960, the live worker stop path did not reliably return on SIGINT because the worker blocks in the module thread.

## Fix Or Workaround

Default PANO customer page to a stable 1920x960 reference-strip preview from six input samples, keep live PANO behind ALLDEMO_PANO_LIVE=1, add page cache, PANO runtime logs, and VO capture support.

## Recurrence Prevention

For PANO customer acceptance, use ./build/alldemo --only PANO default reference mode for clean shutdown; validate ALLDEMO_PANO_LIVE=1 separately before making it the default.

## Related

- SRS-PANO-CUSTOMER-PAGE-001
