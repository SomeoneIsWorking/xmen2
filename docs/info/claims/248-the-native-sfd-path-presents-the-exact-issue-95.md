---
id: C248
kind: claim
status: holds
created: 2026-08-22
tags: native,fmv,graphics,issue-95
depends: src/media/fmv_probe.c#x2_fmv_probe_upload, src/native/movie.c#x2_movie_next_frame, src/d3d8/d3d8_resource.c#d3d8_texture_level_unlocked
---

## Claim

The native SFD path presents the exact issue-95 cine01 story close-up without lower-half corruption and preserves every observed visible row from selected decode through the padded guest image to the D3D8 upload.

## Evidence

2026-08-22 deterministic New Game / Normal replay gated on cine01.sfd: the retained presented-frame-115000 capture matches the original corrupted close-up and is clean; X2_FMV_PROBE reported 4478 decoded frames, 4357 padded checks / 0 mismatched rows, 4357 upload candidates / 4357 exact / 0 mismatched rows, and 4357 complete chains; playback reported 6602752 audio samples and 0 failures. The focused test deliberately mutates one padded row and one upload row and detects both.

## What would falsify it

if the same authored cine01 close-up corrupts under the native path, if any production padded/upload row comparison mismatches, or if the probe no longer detects either deliberate row mutation
