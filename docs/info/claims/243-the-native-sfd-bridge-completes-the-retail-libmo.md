---
id: C243
kind: claim
status: holds
created: 2026-08-22
tags: pc,fmv,integration
depends: src/native/movie.c#x2_movie_next_frame, src/native/movie_image_layout.c#x2_movie_image_pitch
---

## Claim

The native SFD bridge completes the retail libMovie image-to-texture path for the intro movie without decoder or copy failure

## Evidence

A bounded X2_MAX_FRAMES=360 engine run loaded i102.sfd through the native igCriMovieCodec bridge and stopped normally after 360 presented frames. Its live report recorded 236 video decoded, 229 displayed, 347392 audio frames, zero failures. Captures at presented frames 300, 330, 360, 390, and 420 from a second bounded run show the clean Activision logo after guest igImage-to-texture upload. The evidenced guest image allocation was 2097152 bytes for 640x480 display, validated as 1024x512x4 storage with 4096-byte pitch.

## What would falsify it

A bounded native intro run reports a decoder/copy failure, the guest allocation no longer satisfies the evidenced power-of-two layout, or an engine capture shows lower-half block corruption
