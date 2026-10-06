---
id: 95
title: FMV frames contain block-corrupted horizontal bands
status: resolved
symptom: CriMovie playback advances without hanging, but decoded story-movie frames contain blocky horizontal corruption across the lower half of the image
tags: pc,native,fmv,crimovie,graphics,corruption
created: 2026-08-21
updated: 2026-08-22
---

## Root cause

The corruption was introduced inside the retired guest CriMovie decode/output
coordination, before libMovie's shared guest-image-to-D3D8 upload path. The
native replacement changes only the evidenced `igCriMovieCodec` methods; the
same libMovie scene, padded `igImage`, and D3D8 texture upload present the exact
authored close-up without corruption. The legacy decoder's deeper internal
fault was not isolated, and this replacement evidence must not be read as a
more specific attribution.

## What was tried / dead ends

Issue #79's upload-lifetime repair removed the stall but did not remove or
localize the pixel corruption. The first native integration capture used the
`i102.sfd` intro logo; it proved the replacement could cross the shared upload
path, but it could not settle this issue because it was not the authored story
scene where the damage had been observed.

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
isolated. A bounded engine run first proved the native rows survive the guest
image-to-texture upload for `i102.sfd`: frames 300–420 show the Activision logo
cleanly, and the report records 236 decoded / 229 displayed frames, 347,392
audio frames, and zero failures.

### Exact story-scene resolution (2026-08-22)

A deterministic bounded replay entered New Game / Normal with four scripted
inputs and gated observation on the open of `movies/ntsc/eng/c/i/cine01.sfd`.
The retained capture at presented frame 115,000 is the same close-up as
`scratch/screenshots/issue79-bounded-past.png`; its complete lower half is clean.
No scratch path or game asset is tracked.

`X2_FMV_PROBE=cine01.sfd` exercised the production chain rather than a parallel
test implementation. For the complete movie it observed 4,478 decoded frames,
4,357 padded-image checks with zero mismatched rows, 4,357 level-0 upload
candidates, all 4,357 byte-exact with zero mismatched rows, and 4,357 complete
decoded-to-padded-to-upload chains. Playback reported 4,478 displayed frames,
120 dropped frames, 6,602,752 audio samples, and zero failures. The exact
authored scene and every surrounding uploaded frame therefore falsify a native
row-pitch, padding, or upload corruption mechanism and resolve the production
symptom through the native replacement.

### Resolution (2026-08-22)
Native igCriMovieCodec replacement reached the exact cine01 close-up cleanly. Production X2_FMV_PROBE observed 4,357 complete decoded-to-padded-to-D3D8-upload chains, all uploads byte-exact with zero mismatched rows; the shared libMovie upload is ruled out and the retired corruption is confined to guest CriMovie decode/output coordination. The deeper legacy decoder fault was not isolated.
