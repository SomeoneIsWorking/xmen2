---
id: C253
kind: claim
status: holds
created: 2026-08-22
tags: native,fmv,ffmpeg,audio,issue-109
depends: src/media/fmv_decoder_drain.c#x2_fmv_decoder_drain, src/media/fmv_audio_decode.c#flush_tail, src/media/fmv_player.c#pump
---

## Claim

The native SFD player resumes decoder receive after an EOF-flush output queue fills, flushes the audio resampler tail, and reaches FINISHED only after complete source-frame parity.

## Evidence

test_fmv_decoder_drain forces 18 delayed frames through a 16-frame queue: drain call one stops at 16 with flush_sent true and decoder_drained false; call two emits frames 17-18 and reaches decoder EOF. Its audio case emits 2 decoder-delayed and 3 converter-tail outputs. The full real external cine01.sfd test reaches FINISHED at independent ffprobe parity: 4478 video frames and 6602752 audio frames.

## What would falsify it

if a full output queue strands any post-flush frame, if FINISHED occurs before decoder EOF and resampler-tail empty, or if full cine01 decode differs from independent source video/audio frame counts
