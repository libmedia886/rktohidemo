# Helmet detect H264 with B frames stalls NPU demo feed

metadata:
- date: 2026-05-31 17:31:21
- kind: runtime
- status: fixed
- repository: /userdata/alldemo
- summary: HELMET_DETECT_NPU timed out when the demo H264 was encoded as High profile with B frames.

## Symptom

The page initialized VDEC/RGA/NPU/OSD/VO, logged early VDEC packets and VDEC info_change, then produced no HELMET_DETECT_NPU frame-count lines before timeout killed the run.

## Reproduction

```bash
./scripts/check_helmet_detect_demo.sh 40
```

## Evidence

- ffprobe before fix: profile=High, has_b_frames=2.
- ffprobe after fix: profile=Constrained Baseline, has_b_frames=0.
- Passing check: helmet detect check ok: /userdata/alldemo/recordings/helmet_detect_check_20260531_173027/HELMET_DETECT_NPU.run.log

## Affected Files

- assets/loop/helmet_detect/gdut_hwd_helmet_640x640.h264
- scripts/check_helmet_detect_demo.sh
- src/pages/page_helmet_detect_npu.c

## Root Cause

The generated raw H264 used High profile with B frames, while this page feeds Annex-B NAL packets directly into VDEC and the existing demo flow expects baseline/no-B streams like the fruit sample.

## Fix Or Workaround

Re-encoded assets/loop/helmet_detect/gdut_hwd_helmet_640x640.h264 as Constrained Baseline, level 3.1, yuv420p, bframes=0, keyint=30.

## Recurrence Prevention

For alldemo raw-H264 loop assets, verify ffprobe reports profile=Constrained Baseline and has_b_frames=0 before running page checks.

## Related

- None
