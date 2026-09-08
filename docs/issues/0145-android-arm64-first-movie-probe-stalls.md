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
the demuxer has already created them. When the video probe still lacks codec
metadata, `fmv_sfd.c` replays the stream from byte zero, captures the MPEG
sequence dimensions, retains the first audio/video packets, and feeds raw
MPEG chunks through an explicit parser. The audio owner trims only an
incomplete trailing ADX block before sending a packet to FFmpeg. This keeps
the bounded refusal fast while preserving the packet boundaries needed by the
portable ARM64 decoder.

The host test and the exact Android ARM64 standalone test now both decode the
captured `i102.sfd`: 640x480, 312 video frames, 458,656 audio frames, and
changed/distinct decoded frames on both targets. Increasing the probe to 8 MiB
and ten seconds does not solve the boundary; a larger budget is not an
acceptable fix.

The current packaged ARM64 Cuttlefish run reaches the title splash image after
importing the complete install and remains alive with JIT execution and no
Android fatal signal. Around two minutes it has only two presents and the
heartbeat reports a long wait inside the JIT/host boundary, which can make the
screen appear black. A continued run later reached 164 presents and displayed
the X-Men logo, with 89,509 translated blocks and zero JIT refusals. After the
same process remained alive for 996 presents, the ARM64 Cuttlefish screenshot
showed the retail main menu and the status endpoint reported a Vulkan renderer
at 1280x720. This is extreme emulator startup latency rather than a permanent
boot stall. A control-channel key request still timed out because the guest did
not poll the keyboard within five seconds, so interactive menu selection is not
yet evidence.

A fresh setup launch on the same ARM64 Cuttlefish instance requested Android's
notification permission and opened the real DocumentsUI `OPEN_DOCUMENT` picker;
the picker restored its previous `Download/xmen2-emulator-test` location. The
host became unresponsive to subsequent ADB input while the picker was open, so
no selection or interrupted-copy notification trace was recorded. This proves
the picker route is reachable, not that SAF import completion or recreation is
verified.

The installed Google API30 ARM64 AVD cannot provide a second host-side
emulator: Android Emulator 37.1.11 refuses its `arm64` system image on this
x86-64 host (`Avd's CPU Architecture 'arm64' is not supported by the QEMU2
emulator`). Cuttlefish is therefore the available ARM64 emulator here; the
headless API35 image is x86-64 and cannot substitute for ARM64 evidence.

## Remaining falsifier

The remaining falsifier is the packaged APK: it must open the first SFD,
display a movie frame, accept an input into a representative interactive scene,
and survive pause/resume and recreation while the import notification remains
truthful. Until that device run and the named-device performance collection are
captured, Android gameplay and release performance remain partial. Do not skip
the movie or claim a boot fix from JIT counters alone.
