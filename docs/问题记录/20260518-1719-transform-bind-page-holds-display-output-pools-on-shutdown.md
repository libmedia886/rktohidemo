# TRANSFORM bind page holds display output pools on shutdown

metadata:
- date: 2026-05-18 17:19:24
- kind: runtime
- status: resolved
- repository: /userdata/alldemo
- summary: TRANSFORM --only ran all four branches and captured VO frames, but shutdown initially left TRANSFORM/VPSS/OSD/VMIX output pools held. Cleanup now stops display downstream first,...

## Symptom

Baseline TRANSFORM run showed all four counters increasing, then printed pool_id=15, pool_id=14, and pool_id=8/7 refused destroy messages during INT shutdown.

## Reproduction

```bash
timeout -s INT -k 5s 15s stdbuf -oL -eL ./scripts/run_alldemo.sh --only TRANSFORM
```

## Evidence

- /tmp/alldemo_transform_srs001_baseline.log: TRANSFORM reached 382 frames and captured VO BMPs, then pool_id=15/14/8 refused destroy.
- /tmp/alldemo_transform_srs001_after_cleanup.log: upstream pool 15/14 fixed, but DISPLAY_OSD_OUTPUT_POOL 8 was still held by the display chain.
- /tmp/alldemo_transform_srs001_final4.log: four branches reached 383 frames, transform_vo_000150.bmp and 000300.bmp were captured, grep count for rejects/bind failures was 0, and no residual process remained.

## Affected Files

- src/alldemo.c
- README.md
- assets/effect_manifest.json

## Root Cause

The TRANSFORM bind chain was destroying upstream and display output pools before the downstream VO/OSD/VMIX objects had fully released their latest frames. TRANSFORM groups also needed their output queues drained after unbind.

## Fix Or Workaround

Bind TRANSFORM using its actual output port, stop/destroy VO and display modules before upstream TRANSFORM/VPSS cleanup, drain TRANSFORM output queues, and for this bind page skip explicit destroy of display output pools 7/8 so pipeline cleanup releases their last frames before buffer manager shutdown.

## Recurrence Prevention

For bind pages ending in OSD->VO, verify short INT shutdown logs and inspect every pool reject. If the latest display frame is still retained by downstream modules, avoid explicit pool destruction before pipeline cleanup or add a module-side release path.

## Related

- SRS-TRANSFORM-CUSTOMER-PAGE-001
