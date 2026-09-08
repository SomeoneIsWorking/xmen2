# Android arm64 first-movie probe stalls after import

## Symptom

After a complete SAF import on the API 35 ARM64 Cuttlefish device, the APK
maps every PC image and enters the ARM64 x86port JIT without a native crash.
The first movie path can still leave the screen black for more than a minute.

## Cause

The shipped SFD is an MPEG program stream with no program-stream map. FFmpeg's
MPEG-PS demuxer therefore creates the `0x1e0` video stream with a probe request
instead of a codec id. On the portable ARM64 build, `avformat_find_stream_info`
then spends the boot interval probing MPEG packets. The captured stack was in
`ff_read_packet` below `avformat_find_stream_info`; the title file's verified
streams are MPEG-1 video and ADX audio.

## Current correction

The title FMV owner bounds MPEG-PS probing to one MiB and two seconds before
stream discovery, and labels the title's stable `0x1e0`/`0x1c0` stream ids when
the demuxer has already created them. The host FMV decode test still decodes
the user-provided SFD and preserves the tight/padded row checks.

## Remaining falsifier

An Android APK run must open the first SFD, report MPEG-1/ADX rather than an
unsupported stream, display a movie frame, and continue to the menu. Until
that run is captured, Android gameplay and performance remain partial. Do not
skip the movie or claim a boot fix from JIT counters alone.
