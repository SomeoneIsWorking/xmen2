---
id: 67
title: Guest callbacks report a false stack imbalance because their argument cleanup is unspecified
status: resolved
symptom: Every native game boot prints that DirectInput EnumDevices callback returned with ESP 8 bytes higher even though the host did push both stdcall arguments
tags: pc,native,runtime,abi,callback,stack,dinput
created: 2026-08-14
updated: 2026-08-14
---

## Observation

Every full smoke run reported that DirectInput's
`enumerateMouseAndKeyboard` callback returned with ESP eight bytes higher,
calling it a `ret N` whose arguments the host never pushed. `dinput.c` had in
fact reserved and written both callback arguments.

## Root cause

`x86_guest_call` snapshotted ESP after the caller had reserved arguments but
modeled only the return-address pop. It therefore could not distinguish a
correct stdcall `RET 8` from corruption, emitted a false warning, and repaired
the copied CPU to the wrong post-call ESP.

The ambiguity also hid real convention errors: the WinMM `TimeProc` callback
and `EnumSystemLocalesA` callback were routed through a cdecl bridge even though
they return with `RET 0x14` and `RET 4`. Once cleanup became an explicit fatal
contract, it also caught a bad RE reconstruction in the conversation prompt:
`igQuaternionf::igQuaternionf` consumes four floats with `RET 0x10`; the
previous port and documentation had incorrectly attached the already-staged
`$MENU_ACCEPT` draw argument to it as a fifth argument.

## Resolution

The bridge now accepts an explicit callee-cleaned byte count and aborts unless
the returned ESP exactly matches it. The zero-byte wrapper remains for cdecl
and argument-free calls. Every native-to-guest callback site now states its
convention; WinMM and the locale enumerator use a stdcall Ark bridge, and the
conversation constructor has the four arguments demonstrated by its body.

## Verification

The shipping `--selftest` executes a synthetic `RET 8` callback, proves that it
received both arguments, and requires the caller's exact final ESP. A deliberate
zero-byte contract mutation aborts with the expected/actual ESP diagnostic.

`tools/smoke_loop.sh` then completed the 4,200-frame route: all six scripted
inputs fired (last at frame 4,135), no draw was refused, and the final frame had
more than 3,000 distinct colours. Its 1,460-line log contains zero old imbalance
warnings and zero stack-contract violations; the corrected conversation update
ran 1,867 times.

### Resolution (2026-08-14)
Made callee cleanup explicit and fatal at every native-to-guest bridge; corrected stdcall callback sites and the four-float quaternion RE. Battery, mutation, and 4200-frame smoke route pass with zero violations.
