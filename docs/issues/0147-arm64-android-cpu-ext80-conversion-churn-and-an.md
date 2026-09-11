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

## What the device cannot tell us, measured rather than assumed

A 120-second `simpleperf` record at the steady-state title screen, 67,098
samples, attributed by thread:

| thread | samples | share | what it is |
| --- | ---: | ---: | --- |
| `Thread<00..03>` | 57,161 | 85.2% | SwiftShader's four rasteriser workers |
| `SDLThread` | 4,837 | 7.2% | the port: guest execution and the frame loop |
| `AAudio_1`, `SDLAudioP15` | 4,587 | 6.8% | audio mixing and output |
| activity thread | 379 | 0.6% | Android lifecycle |

Inside `SDLThread` the split is 2,766 samples (57.2%) in SwiftShader code
called synchronously on the frame loop, 424 (8.8%) in x86port's translated
guest code in `/memfd:jitcommon-code`, and the remainder in named
`libmain.so`/libc symbols.

So across the whole process the port's translated guest code is **0.63% of
samples** and the emulator's software rasteriser is roughly **89%**. Cuttlefish
has no GPU, so Vulkan is SwiftShader on the same CPUs as the guest. This is the
quantitative reason the device cannot rank port-side optimisations: the port is
not the bottleneck on it, and an optimisation that removed *all* of
`libmain.so`'s named cost would move under 2% of the frame. The named-device
performance gate in S018 stands.

An earlier, narrower reading of this profile (35.7% `[anon:swiftshader_jit]`,
17.5% `vulkan.pastel.so`) undercounted the rasteriser: it filtered by shared
object, and SwiftShader's JIT-compiled rasteriser code lands in small unnamed
anonymous mappings that the DSO view reports as `unknown`. The thread-level
split above is the one to trust.

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

3. **Emulated TLS at the API 21 floor (open, now quantified).** bionic has
   ELF TLS only from API 29, so `__thread` in `libmain.so` compiles to an
   `__emutls_get_address` call with a pthread-key lookup. In the profile above
   it is 66 samples of `SDLThread` -- 1.4% of the port's own thread, and the
   largest named symbol in it after the JIT dispatch loop
   (`x86p_jit_engine_run`, 225) and `syscall` (116). `libmain.so` has only 33
   such call sites over 8 TLS variables, all at transition boundaries rather
   than inside emitted code: `g_fsbase`/`g_gsbase` are copied into
   `cpu->fs_base` once per native-to-guest entry, not per FS-relative access.
   The cost is concentrated in the per-thread guest call stack
   (`x86_guest_call_stack.c`: `x86_guest_call_top` is a further 44 samples) and
   the `guest_thread_*` bookkeeping, where push/pop/top each repeat the lookup.
   The root-cause fix is to thread the call-stack top through the engine's
   existing per-call state so one lookup serves a whole guest call, which works
   at any API floor. Raising the floor to 29 would also remove it but is a
   product decision about device coverage, not a free win.

4. **PLT indirection (done, and it is not a performance lever).** `libmain.so`
   exported every internal helper, so intra-library calls bound through the
   PLT. The Android build now compiles with hidden visibility and links with
   `-Wl,-Bsymbolic-functions`; `main` keeps explicit default visibility for
   SDLActivity's `dlsym`, and JNI entries keep theirs through `JNIEXPORT`.

   Static effect on the arm64-v8a library:

   | | before | after |
   | --- | ---: | ---: |
   | `JUMP_SLOT` relocations | 5,290 | 576 |
   | ...resolving to its own definitions | 4,709 | 0 |
   | exported dynamic symbols | 9,518 | 2,954 |
   | total relocations | 20,331 | 14,833 |
   | stripped library bytes | 8,509,264 | 7,435,520 |

   28,680 `bl ...@plt` call sites became direct branches, including most of the
   runtime's own (`x86p_*` 371 -> 49, `jit*` 164 -> 26).

   Runtime effect: none that can be measured. Both stripped libraries were run
   on the same device, same install, same title-screen scene, for a 300-second
   window each, reporting host QEMU CPU-seconds per presented frame -- under
   TCG that is a work measure, not a phone frame time:

   | | frames | host CPU s/frame | p50 |
   | --- | ---: | ---: | ---: |
   | hidden visibility | 254 | 4.2257 | 1169.7 ms |
   | default visibility | 253 | 4.2339 | 1172.6 ms |

   A 0.19% difference, inside run-to-run noise. That is consistent with the
   profile: all of `libmain.so`'s named code is 876 of 67,098 samples (1.3%),
   and the PLT was a fraction of that. The change is kept for the 1.07 MB and
   6,564 exported symbols it removes from the shipped library, not for speed,
   and it should not be cited as a performance win.

5. **Every Jcc and SETcc called `x86p_cond` (fixed, x86port `ec7d284`).** The
   backend evaluated each condition by calling the shared authority, which
   re-derives the predicate from the lazy-flag record the host had just
   computed. `jit_arm64_cond.c` now lowers it onto AArch64's own NZCV for the
   Add, Sub, Logic, Inc and Dec kinds at width 4 -- one `cmp`/`cmn` and a
   `cset`. `x86p_cond` remains the authority and still serves PF, narrower
   widths, and the Inc/Dec conditions that read CF or OF.

   How often it fires had to be measured on the title's own code, because the
   generated differential almost never places a width-4 ALU immediately before
   a branch and reported only 32 of 249 (12.8%). The engine publishes both
   counts, and the running game on this device reports:

   **14,145 of 18,497 conditions (76.5%) lowered inline**, over 65,319
   translated blocks and 312,934 guest instructions.

   Correctness is the interpreter differential on AArch64 under
   `qemu-aarch64`: 25,155 checks, 0 failures, 1,423 programs, 823 of the blocks
   ending in a translated branch.

   No frame-time claim is attached. A 120-second `simpleperf` profile of the
   running game afterwards puts `x86p_cond` at 117 of 7,451 `libmain.so`
   samples (1.6%) against `x86p_jit_engine_run`'s 684 -- but that scene is an
   FMV, not the title screen the PLT A/B used, so it is not a before/after
   comparison and is not offered as one.

6. **The next two costs this profile names, both larger than what was just
   fixed.** Same 120-second FMV profile, `libmain.so` samples:

   | symbol | samples | % of libmain.so |
   | --- | ---: | ---: |
   | `x86p_jit_engine_run` | 684 | 9.25% |
   | `yuv2rgb_c_32` | 636 | 8.52% |
   | `x86_engine_jit_intercept` | 366 | 4.98% |
   | `x86_guest_call_top` | 331 | 4.50% |
   | `x86p_x87_arith` | 302 | 4.10% |
   | `__letf2` | 296 | 4.00% |
   | `ff_simple_idct_put_int16_8bit` | 261 | 3.52% |
   | `x86p_flag_cf` | 237 | 3.21% |
   | `x86p_cond` | 117 | 1.59% |

   - **FFmpeg is running its scalar C paths.** `yuv2rgb_c_32`,
     `ff_simple_idct_*` and `idctRowCondDC_int16_8bit` are the portable
     implementations: the shared Android owner configures FFmpeg with
     `--disable-asm` on arm64 (`android-port` `1161a40`) because FFmpeg
     7.1.1's AArch64 transform assembly does `adrp`+`add` against the global
     `ff_tx_tab_*` tables, which a PIE shared object cannot relocate. That
     workaround disables ALL NEON, not just the transform, and together those
     three symbols are 13.7% of `libmain.so`. The narrower fix is to build
     FFmpeg with `-fvisibility=hidden` so those tables stop being preemptible
     and the assembly's relocation becomes legal; nothing outside `libmain.so`
     needs FFmpeg's symbols. That belongs in `shared/android-port` and affects
     every Android port consuming the prefix.
   - **`x86p_flag_cf` is twice `x86p_cond`.** Carry-in derivation is still a
     call per ADC/SBB/RCL/RCR; the same NZCV argument that retired most
     condition helper calls applies to it and has not been made yet.
