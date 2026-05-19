# VI 4K input bind path has no screen output

metadata:
- date: 2026-05-19 09:33:38
- kind: validation
- status: open
- repository: /userdata/alldemo
- summary: VI 3840x2160 could start and bind, but the screen stayed blank in field observation.

## Symptom

User reported no screen image after switching VI page to 3840x2160, while the previous VI page had image.

## Reproduction

```bash
timeout -s INT -k 5s 8s ./build/alldemo --only VI
```

## Evidence

- MEDIA_VI_SetAttr succeeded at 3840x2160@30fps and bind compatibility checks passed.
- User confirmed the screen had no image, although the earlier VI page had image.
- Runtime log showed VI input=3840x2160 display=1080x608 with resize_frames=0.

## Affected Files

- src/alldemo.c
- docs/AI团队/demo需求/SRS-VI-CUSTOMER-PAGE-001.yaml
- README.md

## Root Cause

Suspected cause: the VI 4K -> RESIZE_RGA -> VMIX display chain binds successfully but RESIZE_RGA/VMIX does not produce visible output for this 4K input path; runtime logs showed resize_frames staying 0.

## Fix Or Workaround

Changed the default VI page input to 1920x1080 and kept the RESIZE_RGA 1080x608 display path, matching the product rule to fall back to 1080P when 4K cannot be shown.

## Recurrence Prevention

When enabling 4K input, require live screen confirmation or a positive downstream output counter before marking the page accepted; do not rely on VI frame count alone.

## Related

- SRS-VI-CUSTOMER-PAGE-001
