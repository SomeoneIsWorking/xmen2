---
id: C036
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The xboxrecomp runtime builds completely on Linux after four upstream portability fixes; all seven libraries including the Xbox kernel HLE now compile.

## Evidence

cmake -S . -B build && cmake --build build now returns 0 and produces libplatform.a, libxbox_kernel.a, libxbox_d3d8.a, libxbox_dsound.a, libxbox_input.a, libxbox_apu.a, libxbox_nv2a.a. Four fixes, all real Windows-vs-Linux differences rather than build-flag tweaks: (1) wcslen used on a WCHAR string -- Xbox WCHAR is UTF-16 (2 bytes) while glibc wchar_t is 4, so wcslen walks off the end; replaced with an explicit 16-bit counter, and MSVC only agreed by coincidence. (2) ERROR_NOT_OWNER missing from the Win32 vocabulary header. (3) IsDebuggerPresent/DebugBreak absent; implemented as no-debugger plus __builtin_trap, deliberately reporting FALSE so the game does not take its debug paths. (4) a missing include. Kept as patches/xboxrecomp/0001-linux-portability.patch.

## What would falsify it

The libraries COMPILE; none has been exercised. Nothing links the 1.5M lines of lifted game code against them yet, and the NV2A and APU layers in particular are xemu code whose behaviour under this harness is untested here.
