# NPU standalone VDEC pool and RGB drain

metadata:
- date: 2026-05-26 11:38:41
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: NPU standalone initially failed VDEC buffer sizing and then leaked one RGB pool buffer on shutdown.

## Symptom

VDEC info_change reported need=819200 have=737280; after resizing, shutdown reported pool_id=4 still had 1 buffer not returned.

## Reproduction

```bash
timeout -s INT -k 3s 8s ./scripts/run_alldemo.sh --only NPU
```

## Evidence

- VDEC info_change: 640x640 stride=640x640 buf_size=819200 pool_size=737280
- pool_id=4 still has 1 buffer not returned; refused destroy

## Affected Files

- src/pages/page_npu.c

## Root Cause

The old NPU VDEC frame-size macro underallocated the decoder output pool for the current VDEC info_change requirement, and NPU passthrough output frames needed explicit drain before destroying the RGB pool.

## Fix Or Workaround

Set NPU_VDEC_FRAME_SIZE to NPU_STRIDE * NPU_H * 2, then drain MEDIA_NPU_GetResult and MEDIA_NPU_GetFrame during page_npu cleanup.

## Recurrence Prevention

For VDEC-backed pages, compare pool size against info_change buf_size; for passthrough modules, drain both result and frame queues before destroying output pools.

## Related

- SRS-ALLDEMO-ARCHITECTURE-SPLIT-037
