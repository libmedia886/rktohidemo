# DCP live dehaze 1280x720 backpressure exhausts CSC output

metadata:
- date: 2026-05-26 13:42:19
- kind: validation
- status: fixed
- repository: /userdata/alldemo
- summary: DCP_FAST_DEHAZE at 1280x720 could run but intermittently exhausted CSC_RGA output buffers; 960x540 keeps the live chain clean.

## Symptom

CSC_RGA: 申请输出 buffer 失败

## Reproduction

```bash
timeout -s INT -k 3s 8s ./scripts/run_alldemo.sh --only DCP_FAST_DEHAZE
```

## Evidence

- At 1280x720, DCP counts lagged: vi=214 csc=187 dehaze=184 resize=184 osd=183 vo=183.
- At 960x540, final validation reached vi=214 csc=214 dehaze=213 resize=213 osd=213 vo=213 with overlay=perf_text and no CSC_RGA buffer errors.

## Affected Files

- src/pages/page_cap_dehaze_offline.c

## Root Cause

DCP_FAST_DEHAZE processing at 1280x720 could not consistently keep up with the live camera/CSC path, so CSC_RGA temporarily ran out of output buffers under backpressure.

## Fix Or Workaround

Keep the DCP live page on real VI, but insert a pre RESIZE_RGA stage to 960x540 before CSC_RGA and DCP_FAST_DEHAZE.

## Recurrence Prevention

When validating OpenCL-heavy live pages, check stderr for buffer exhaustion as well as downstream frame counters; lower the processing resolution before increasing pools blindly.

## Related

- None
