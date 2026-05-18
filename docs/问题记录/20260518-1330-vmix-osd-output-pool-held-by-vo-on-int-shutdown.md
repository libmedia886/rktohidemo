# VMIX OSD output pool held by VO on INT shutdown

metadata:
- date: 2026-05-18 13:30:04
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: Short customer-demo shutdown initially left DISPLAY_OSD_OUTPUT_POOL with one buffer still held.

## Symptom

Exit log printed: pool_id=8仍有1个buffer未归还，拒绝销毁

## Reproduction

```bash
timeout -s INT 6s ./scripts/run_alldemo.sh --customer-demo --no-rotate-main
```

## Evidence

- First short customer-demo shutdown printed `pool_id=8仍有1个buffer未归还，拒绝销毁`.
- After reordering cleanup, the same short-run path printed `Pool销毁完成: id=8` and no residual `alldemo` process remained.

## Affected Files

- src/alldemo.c

## Root Cause

cleanup_display_vmix_osd destroyed the OSD output pool before stopping and destroying VO, while VO could still hold the latest OSD output frame.

## Fix Or Workaround

Reordered normal cleanup to stop/destroy VO before cleanup_display_vmix_osd destroys VMIX/OSD output pools.

## Recurrence Prevention

For bind chains ending in VO, unbind first, then stop/destroy VO before destroying the upstream output pool that feeds VO.

## Related

- None
