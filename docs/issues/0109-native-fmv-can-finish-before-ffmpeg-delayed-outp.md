---
id: 109
title: Native FMV can finish before FFmpeg delayed output is drained
status: resolved
symptom: At SFD EOF, a full 16-frame output queue can strand delayed video frames and resampler-tail audio while the player reports finished.
tags: pc,native,fmv,ffmpeg,decode,eof,audio
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

`flushed_video` and `flushed_audio` meant only that a NULL packet had been sent. The same booleans were used as proof that receive-side draining was complete. If video receive filled the 16-frame queue after the NULL packet, later updates never called `avcodec_receive_frame` again; finish tested the sent flag and an empty queue, not `AVERROR_EOF`. Audio likewise never flushed the resampler tail.

## Resolution

`fmv_decoder_drain` now owns separate flush-sent, decoder-drained, and converter-tail-drained states. Queue backpressure returns incomplete and later updates resume receive until decoder EOF. `fmv_audio_decode` owns ADX receive/resampling and flushes the resampler with NULL input until empty. The focused fake forces 18 delayed frames into a 16-frame queue and proves frames 17-18 resume; its audio case proves delayed decoder and converter-tail output. Full real `cine01.sfd` drain matches independent ffprobe counts: 4,478 video and 6,602,752 audio frames.
