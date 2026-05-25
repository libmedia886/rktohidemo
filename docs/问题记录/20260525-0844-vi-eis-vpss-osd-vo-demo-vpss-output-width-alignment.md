# VI EIS VPSS OSD VO demo VPSS output width alignment

metadata:
- date: 2026-05-25 08:44:09
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: New VI->EIS->VPSS->OSD->VO demo failed until VPSS output width was changed from 1080 to 1072 because VPSS requires 16-aligned output width.

## Symptom

VPSS rejected the output attr during setup: 'VPSS 模块输出 0: output: 宽度必须16对齐 width=1080'.

## Reproduction

```bash
timeout -s INT -k 3s 12s ./scripts/run_alldemo.sh --only EIS_VI
```

## Evidence

- Failing run: VPSS 模块输出 0: output: 宽度必须16对齐 width=1080
- Passing run: EIS_VI frame-counts reached VI/EIS/VPSS/OSD/VO after using 1072x608 output.

## Affected Files

- src/alldemo.c
- README.md

## Root Cause

VPSS output width validation requires width to be 16-aligned; 1080-wide portrait display content is not valid as a direct VPSS output even though the stride was 64-aligned.

## Fix Or Workaround

Changed the EIS_VI page output from 1080x608 to centered 1072x608 with stride 1088, then rebuilt and verified downstream VI/EIS/VPSS/OSD/VO frame counts.

## Recurrence Prevention

For any direct VPSS output that feeds display, align the output width to 16 first, then choose stride with 64-byte alignment and center it in the 1080-wide screen if needed.

## Related

- None
