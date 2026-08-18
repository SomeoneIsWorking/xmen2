---
id: C209
kind: claim
status: holds
created: 2026-08-16
tags: override,recompiler,runtime,refactor
---

## Claim

Native overrides no longer need `--wrap`/`--isolate`/a JSON generator: an override is declared in C by `x86_register_override("<module>", 0x…, fn)`, the emitter routes every call (direct AND vtable) to a registered entry point through the dispatcher's override slot, and the game still boots straight into a level and closes the full smoke loop

## Evidence

The 2026-08-16 refactor removed `overrides.json`, `tools/gen_overrides.py`, the `-Wl,--wrap` flags, the `--isolate` lists, and `__wrap_`/`__real_` symbols from the override path. In their place: `x86_register_override(const char *module, uint32_t linked_ep, x86_override_fn fn)` (src/native/x86rt_native.c) records (module, linked EP)->native-C in a fixed table, which `x86_overrides_resolve()` converts to mapped addresses once pe_map has placed every module; the bare-address form this claim was first written against was wrong for every relocated DLL (see C212); `x86_native_call_at` consults it AFTER import thunks and BEFORE the recompiled-body lookup, so the override path skips the module scan and the epcount/ring bookkeeping (the frame-cap override runs every frame). `recomp.py` (via tools/recomp_overrides.py) scans `src/native/*.c` for `x86_register_override(0x…` literals, intersects with the module being emitted, and emits `DISPATCH`/`TAIL_DISPATCH` instead of a direct C call for any call targeting an overridden EP -- verified: for all 17 override EPs there are ZERO `fn_<mod>_<ep>(C)` call sites left in the emitted chunks, and the DX-check call site (0x00617480, the direct-call case that was the crux) is now `DISPATCH(C, (G_IMGBASE + 0x217480U))`. An override that defers to the original calls the body's `fn_<mod>_<ep>` symbol directly (the body stays emitted and linked). The oracle probes keep `--wrap`/`--isolate` (a recording hook is not a replacement); `tools/gen_probes.py` now writes the isolate lists itself, the only remaining writer.

Verified end-to-end on the final build: `X2_BOOT_MAP=act0/tutorial/tutorial1` run opens the tutorial level (scene gate at the tutorial1.pkgb open), the DirectX check and X2_UNPACED frame-cap overrides both announce, 0 draws refused, clean exit; `tools/smoke_loop.sh` passes all four checks on the normal menu-driven boot path (12/12 presses, level loaded, no refusal, 3000+ colours); 49/49 ctests pass; `--probe-selftest` binds 10/10 probe wrappers.

Also fixed while landing it: `recomp.py emit` now removes ORPHANED chunk files an earlier emit left past its count (a shrinking isolate list left stale-stamped chunks that the build globbed), and `tools/smoke_loop.sh` counted press WINDOWS (4) where the game schedules press EVENTS (12), so the "every press fired" check failed on every run since the repeat windows were introduced (commit 086588f); `tools/drive.sh count` now reports the event count.

## What would falsify it

a run where an override's announcement never prints while its EP is registered (registration or routing broken), a direct call to an overridden EP still linking to `fn_<mod>_<ep>` (emitter scan missing it), or the smoke loop failing to close on the normal boot path