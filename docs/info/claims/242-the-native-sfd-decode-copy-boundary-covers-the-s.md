---
id: C242
kind: claim
status: holds
created: 2026-08-22
tags: pc,fmv,media
depends: src/media/fmv_player.c#x2_fmv_copy_bgra, tests/test_fmv_decode.c#main
---

## Claim

The native SFD decode/copy boundary covers the shipped codec set and preserves every decoded row

## Evidence

ffprobe inventory of cine01-cine07 and i101-i107 found MPEG-PS, 640x480 MPEG-1 video, and 44.1 kHz stereo ADX in all 14 files. The production test against external cine01.sfd decoded 177 changed/119 distinct frames and 288256 audio frames; tight and padded BGRA copies matched on all 480 rows with padding sentinels intact and non-uniform lower-half picture content.

## What would falsify it

A shipped SFD probes as another container/codec combination, the production test finds any mismatched row or overwritten padding, or a native decoded lower half is block-corrupt before guest texture upload
