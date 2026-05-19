# Display VMIX output pool retained one buffer on page shutdown

metadata:
- date: 2026-05-18 18:06:01
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: OSD/RGA/CSC_CL default-loop pages logged pool destroy refusal until display VMIX/VMIX_RGA and direct OSD output queues were drained before pool destroy.

## Symptom

The default customer sequence advanced to the next page, but OSD, RGA, and CSC_CL cleanup logged 'pool_id=7仍有1个buffer未归还，拒绝销毁'.

## Reproduction

```bash
timeout -s INT -k 30s 145s stdbuf -oL -eL ./scripts/run_alldemo.sh > /tmp/alldemo_customer_default_full_loop_20260518.log 2>&1
```

## Evidence

- /tmp/alldemo_customer_default_full_loop_20260518.log:578 OSD pool_id=7仍有1个buffer未归还，拒绝销毁
- /tmp/alldemo_customer_default_full_loop_20260518.log:700 RGA pool_id=7仍有1个buffer未归还，拒绝销毁
- /tmp/alldemo_osd_cleanup_fix.log and /tmp/alldemo_rga_cleanup_fix2.log: capture plus Pipeline destroyed and no 拒绝销毁
- /tmp/alldemo_customer_default_full_loop_fix.log:1046 CSC_CL pool_id=7仍有1个buffer未归还，拒绝销毁
- /tmp/alldemo_csc_cl_cleanup_fix.log: CSC_CL capture plus Pipeline destroyed and no 拒绝销毁

## Affected Files

- src/alldemo.c
- docs/问题记录/INDEX.md

## Root Cause

DISPLAY_VMIX_OUTPUT_POOL can still hold a queued VMIX/VMIX_RGA output buffer after OSD->VO and VMIX->OSD unbind; cleanup drained OSD output only and then destroyed pool 7. For CSC_CL, the same numeric pool id is OSD_OUTPUT_POOL, and its direct OSD->VO path also needed to drain OSD output before destroy.

## Fix Or Workaround

Added drain_display_vmix_output() and drain_display_vmix_rga_output(), called before destroying display VMIX/VMIX_RGA groups and output pools. Also drained DISPLAY_OSD_GRP output in cleanup_live_csc_cl_chain() before stopping/destroying the direct CSC_CL OSD stage.

## Recurrence Prevention

When adding bind display pages that feed VMIX, VMIX_RGA, or direct OSD into VO, verify cleanup with a timeout-bound --only run and grep for 拒绝销毁 before accepting the page.

## Related

- SRS-OSD-CUSTOMER-PAGE-001
- SRS-RGA-CUSTOMER-PAGE-001
- SRS-CSC-CL-CUSTOMER-PAGE-001
