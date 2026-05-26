# short overlay probes left alldemo processes after timeout

metadata:
- date: 2026-05-26 13:10:24
- kind: validation
- status: resolved
- repository: /userdata/alldemo
- summary: Short OSD overlay probes can leave alldemo processes after timeout; always run pgrep cleanup before finalizing.

## Symptom

After VPSS/OSD/TRANSFORM timeout probes, pgrep still showed ./build/alldemo and /userdata/alldemo/build/alldemo --only VPSS.

## Reproduction

```bash
timeout -s INT -k 3s 7s ./scripts/run_alldemo.sh --only VPSS
```

## Evidence

- pgrep -af '/userdata/alldemo/.*/alldemo|build/alldemo|run_alldemo.sh' returned PIDs 1610320 and 1732166.
- Runtime logs also showed repeated vo_control_commit_planes: timeout waiting for flip (EBUSY retry).

## Affected Files

- src/pages/page_vpss.c
- src/pages/page_osd.c
- src/pages/page_transform.c
- scripts/run_alldemo.sh

## Root Cause

suspected cause: timeout interrupted the probe while DRM plane flips were retrying, leaving one or more alldemo processes alive.

## Fix Or Workaround

Sent kill -INT to the residual PIDs, then reran pgrep and confirmed no matching alldemo/run_alldemo.sh process remained.

## Recurrence Prevention

After every timeout-based display probe, run pgrep -af '/userdata/alldemo/.*/alldemo|build/alldemo|run_alldemo.sh' and clean residual processes before reporting validation complete.

## Related

- SRS-ALLDEMO-ARCHITECTURE-SPLIT-044
