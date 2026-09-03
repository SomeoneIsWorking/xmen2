---
id: C278
kind: claim
status: holds
created: 2026-09-03
tags: 
depends: src/native/audio_adpcm.c, src/native/audio_adpcm_verify.c
---

## Claim

XMen2.exe's statically-linked IMA ADPCM decoders (0x00616770 mono, 0x00616880 stereo) are natively owned and bit-exact vs the guest body

## Evidence

test_audio_adpcm bit-matches both overrides against an independent IMA reference (40 mono samples + 24 stereo frames, state write-back, cdecl esp). Driven in-game with --set audio.adpcm_verify=1: first native decode of a 1552-byte mono stream matched the guest body, 0 disagreements over the session. With verify off, 0x006167xx/0x006168xx blocks leave the jit.profile top-40 entirely (were ranked 10-27, ~0.6% each).

## What would falsify it

audio.adpcm_verify aborts in a driven session, or 0x006167xx/0x006168xx reappears in jit.profile with the overrides registered, or the guest decoder tables at 0x006e95a8/0x006e9588 differ from the canonical IMA tables in a retail image revision
