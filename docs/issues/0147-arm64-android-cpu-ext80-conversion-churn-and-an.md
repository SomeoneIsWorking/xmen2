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

7. **Both of those leads are now closed, and the emulated-TLS one with them.**

   - *FFmpeg scalar paths.* `shared/android-port` builds the prefix with
     `-fvisibility=hidden` instead of `--disable-asm`; the tables stop being
     preemptible, the `adrp`+`add` relocation becomes legal, and 205 NEON
     symbols appear in `libmain.so`. `ff_yuv420p_to_bgra_neon` (11.83%)
     replaced `yuv2rgb_c_32` (8.52%) and the scalar IDCT symbols are gone.
   - *Carry-in derivation.* It was not the ADC/SBB case at all: `x86p_flag_cf`
     reads `carry_in` only for the Inc and Dec lazy-flag kinds, so for a binary
     ALU -- which records Add, Sub or Logic -- both the derivation and the
     `FLAG_CARRY_IN` store were dead. `flags.h` now owns that rule as
     `x86p_flags_carry_in_is_live` and both backends gate on it (x86port
     `b7f215e`). Carry-in helper calls over the AArch64 differential corpus
     fell from 789 to 268 across 1,423 blocks; on the device `x86p_flag_cf`
     went from 5.07% to 1.53% of `libmain.so` samples.
   - *`x86_guest_call_top`.* At 13.44% it was the second cost in the profile
     taken after the FFmpeg fix. The cause was not the function: an Android
     shared object below API 29 has EMULATED thread-locals, so reading the
     per-thread call-frame head is a call through a pthread key, and
     `x86_engine_jit_intercept` does it once per block boundary.
     `tls_model("initial-exec")` is already requested and emulated TLS ignores
     it. The fix is at the boundary that needs the value:
     `x86p_jit_engine_run` now takes the caller's per-run state and hands it to
     the intercept and dispatch callbacks (x86port `09573fa`), so the title
     passes the frame it just pushed. Living on the run's own stack it is
     per-thread by construction, unlike the registered user pointer one engine
     shares between guest threads. `x86_guest_call_for_cpu` is deleted; the
     CPU-ownership check moved to where the frame is consumed.

   After it, `x86_guest_call_top` has ZERO samples in a 20-second profile
   (22,460 samples, 5,381 in `libmain.so`) and `x86_engine_jit_intercept` is
   3.12%, down from 11.78%. The two profiles are different phases of boot --
   this one is dominated by `x86p_mem_write_bytes` at 27.58% -- so the shares
   are not a like-for-like before/after; the symbol's disappearance is the
   claim, and its only remaining callers are the cold fault reporter and
   `x2_engine_where`.

   The next cost this names is `x86p_mem_write_bytes` plus `x86p_mem_write` and
   `x86p_string_execute`: a REP MOVS-heavy asset phase going through the
   byte-at-a-time memory path.

8. **A forward REP STOS now fills its span in one call** (x86port `6c277f4`).
   61% of `x86p_mem_write_bytes`'s samples arrived through
   `step_once` <- `x86p_string_execute` <- `jit_string`: a guest memset paying
   an accessibility check and a four-byte `memcpy` per element, while the
   guest memcpy beside it had had a bulk path since `x86p_mem_copy_disjoint`.
   `x86p_mem_fill` carries the identical admission rule, and every refusal
   keeps element-wise execution so a partial fault still leaves ECX and EDI
   exactly where the guest would see them.

   On the device the new symbol is reached -- `x86p_mem_fill` is 1.38% of all
   samples, 65% of them under `x86p_string_execute` -- and `step_once` is down
   to 0.13% with `x86p_mem_write_bytes` out of the ranked list entirely. As
   with every step here the profile is a different phase of boot, so that is
   evidence the path fires in the shipping build rather than a before/after
   share.

   What the same profile now names, with the memory path gone: the intercept
   predicate itself at 18.14% behind `x86p_jit_engine_run`'s 33.14%, and
   `x86p_cond` back at 10.97% -- the 23.5% of conditions the ARM64 inline
   lowering still refuses.

9. **Condition lowering now covers byte and word widths, and a branch reads the
   host condition directly** (x86port `5396aa2`, `28a7635`). The lowering was
   width-4 only, which is why `x86p_cond` came back at 10.97% once the memory
   path was fixed. Left-aligning the flag operands (`lsl` by 32 - 8w) makes the
   32-bit NZCV exactly the narrow operation's, and the shift also discards the
   bits above the width that flags.c masks on read rather than on store. A Jcc
   then feeds that condition straight into the `csel` that picks its successor
   instead of materialising 0/1 with a `cset` and testing it again.

   On the device the running title went from **76.5% of its conditions lowered
   inline to 98.2%** (15,777 of 16,059), `x86p_cond` to 0.51% of `libmain.so`
   samples, and the `csel` build reports 4,062 of 4,096 (99.2%) over its first
   ten seconds. 25,155 differential checks against the interpreter oracle under
   `qemu-aarch64`, zero failures.

   **What is left is the dispatch loop itself.** In the same profile
   `x86p_jit_engine_run` is 37.06% and `x86_engine_jit_intercept` 16.68% --
   over half the port library's samples spent leaving a block, consulting the
   intercept, hashing the next EIP and re-entering, with the emitted code
   itself accounted separately under the unknown DSO. No block is chained to
   its successor: every exit returns to the C run loop. Chaining is admissible
   for a static successor the boundary predicate already cleared at translation
   time, plus an inline compare against the run's `return_to` -- the intercept's
   only dynamic condition. That is the next structural win and it is
   substantially larger than anything above it here.
