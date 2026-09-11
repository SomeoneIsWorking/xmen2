---
id: 147
title: ARM64 Android CPU: ext80 conversion churn and an ungated per-upload texture scan
status: open
symptom: on Android ARM64 every guest x87 op pays two binary128->ext80 softfloat conversions, and every level-0 texture upload is scanned pixel by pixel by a diagnostic that is always on
tags: android,arm64,performance,x87,x86port,d3d8,diagnostic
created: 2026-09-11
updated: 2026-09-11
---

## What was run

The packaged debug ARM64 APK (NDK 28, API 21 floor) on the shared ARM64
Cuttlefish device (`aosp_cf_arm64_only_phone`, Android 17, QEMU TCG, guest
SwiftShader), with the complete 2.37 GiB PC install staged into app-private
`files/debug-game` and selected through the debug private-install extra.

It maps the PC images, enters the ARM64 JIT and reaches the X-Men Legends II
title screen: 209 presents, Vulkan backend at 1280x720, 108.5M JIT block
entries, 79,315 translated blocks, 380,571 translated instructions, zero
refusals and no fallback. A `Return` press through the loopback control channel
was accepted by the guest at frame 203. Screenshots:
`scratch/android/arm64-run.png`, `scratch/android/arm64-after-input.png`.

## What the device cannot tell us

`simpleperf` over the running process: 35.7% of samples are in
`[anon:swiftshader_jit]` and a further 17.5% in `vulkan.pastel.so` -- the
emulator's software rasteriser -- while the guest CPU itself is QEMU TCG.
Frame times (p50 815 ms) are an emulator artefact. This device answers
correctness questions only; the named-device performance gate in S018 stands.

## Findings that are host-independent

1. **x87 conversion churn (fixed, x86port `34e1a9e`).** On a binary128 host
   every guest x87 arithmetic operation widened both operands and narrowed the
   result through the general softfloat `f128_to_extF80`/`extF80_to_f128`.
   Those values are finite normals both formats hold exactly, so the conversion
   is bit reassembly. NDK arm64 micro-benchmark under `qemu-aarch64`:
   multiply/add pair 214 -> 145 ns per op, against 99 ns for the softfloat
   arithmetic alone. Verified by `test_x87_f128_ext80` (28,372 cases against
   softfloat itself) run natively AND on this ARM64 device, with the existing
   3,140 software-math and 341 narrowing checks unchanged.

2. **Ungated per-upload texture brightness scan (fixed here).**
   `d3d8_texture_luma_note` ran a full floating-point pass over the bytes of
   every level-0 texture upload on every upload -- about a megabyte of work per
   movie frame -- and was 3.5% of `libmain.so` samples during boot. It is a
   diagnostic; it is now armed with `X2_TEXTURE_LUMA=1` and its report says
   NOT MEASURED when it was not armed, instead of printing an empty table.
   `test_d3d8_texture_luma` now covers the disarmed case as well.

3. **Emulated TLS at the API 21 floor (open).** `__emutls_get_address` was
   2.4% of all samples and `x86_guest_call_top` a further 1.6%: bionic has ELF
   TLS only from API 29, so `__thread` in `libmain.so` compiles to a function
   call with a pthread key lookup. The two hot users are the per-thread guest
   call stack (`x86_guest_call_stack.c`) and `g_fsbase`/`g_gsbase`. Either
   thread the call-stack top through the engine's existing per-call state, or
   raise the API floor -- which is a product decision about device coverage,
   not a free win.

4. **PLT indirection (open, unquantified).** `@plt` was 12% of `libmain.so`
   samples during boot and 1.6% overall later. `libmain.so` exports its
   internal helpers, so intra-library calls bind through the PLT.
   `-fvisibility=hidden` plus `-Wl,-Bsymbolic-functions` would make them
   direct; it needs measuring on a host where the measurement means something,
   not on this device.
