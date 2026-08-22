---
id: 95
title: FMV frames contain block-corrupted horizontal bands
status: open
symptom: CriMovie playback advances without hanging, but decoded story-movie frames contain blocky horizontal corruption across the lower half of the image
tags: pc,native,fmv,crimovie,graphics,corruption
created: 2026-08-21
updated: 2026-08-22
---

## Root cause


## What was tried / dead ends


## Resolution

### Note (2026-08-21)
Observed after the issue #79 queue-lifetime fix on a screenshot-confirmed story FMV at presented frame 20,210. The movie continued advancing at roughly 80-110 frames/s, so this is not a frozen/stale frame. scratch/screenshots/issue79-bounded-past.png shows correct upper image content with blocky horizontal corruption across the lower half. Upload lifetime is ruled out as the hang mechanism, not as the pixel mechanism: determine whether corruption already exists in the guest staging bytes before UnlockRect, then compare row pitch/dirty rectangle and the stock decoder output before changing the renderer.

### Native replacement progress (2026-08-22)

The requested native replacement now owns only MPEG-PS/MPEG-1/ADX decode and
streams its output through the retail `igCriMovieCodec` lifecycle documented in
`docs/RE/fmv.md`. A real external `cine01.sfd` decoder test produced 177 changed
frames, 119 distinct hashes, and 288,256 decoded audio frames. Its tight and
padded BGRA copies matched on all 480 rows and preserved every row-padding
sentinel; lower-half picture range was non-uniform. This proves the native
decode/copy boundary does not reproduce the old lower-half damage.

Do not call the old decoder's precise corruption mechanism solved: it was not
isolated. A bounded engine run now proves the native rows survive the guest
image-to-texture upload for `i102.sfd`: frames 300–420 show the Activision logo
cleanly, and the report records 236 decoded / 229 displayed frames, 347,392
audio frames, and zero failures. Keep this issue open until the later story FMV
where it was originally observed is captured through the native path; an intro
logo is complete integration evidence, but it is not that authored scene.
