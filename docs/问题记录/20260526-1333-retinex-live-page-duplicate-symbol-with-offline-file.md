# RETINEX live page duplicate symbol with offline file

metadata:
- date: 2026-05-26 13:33:17
- kind: link
- status: fixed
- repository: /userdata/alldemo
- summary: Adding page_retinex.c with page_retinex_run conflicted with an existing symbol in page_retinex_offline.c.

## Symptom

Link failed: multiple definition of page_retinex_run; page_retinex_offline.c and page_retinex.c both defined it.

## Reproduction

```bash
cmake --build build -j
```

## Evidence

- /usr/bin/ld: page_retinex_offline.c:(.text+0x1540): multiple definition of 'page_retinex_run'

## Affected Files

- src/pages/page_retinex.c
- src/pages/page_retinex_offline.c
- src/pages/page_ops.c
- CMakeLists.txt

## Root Cause

page_retinex_offline.c already contained a synthetic page_retinex_run stub even though RETINEX was registered through the legacy proxy.

## Fix Or Workaround

Renamed the new live implementation to page_retinex_live_run and registered RETINEX to that symbol.

## Recurrence Prevention

Before adding a new page file, grep existing page_* files for the run symbol, especially files that also host offline variants.

## Related

- None
