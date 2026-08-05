---
id: 10
title: macOS __PAGEZERO makes the low 4 GB unmappable, which is exactly where the guest image has to go
status: open
symptom: On macOS, pe_map's MAP_FIXED_NOREPLACE at 0x10000000 (or 0x400000 for XMen2.exe) fails, so x2native reports that the kernel would not place the image at its preferred base and refuses to run
tags: pc,recomp,native,macos,portability,rc-native
created: 2026-08-05
updated: 2026-08-05
---

## Why this is written down before anyone hits it

SDL3 GPU was chosen partly because it maps onto Metal on macOS. That makes
macOS a real target, and the native design has a hard assumption that collides
with it.

## The collision

`src/native/pe_map.c` maps the original PE at its OWN preferred base and
REFUSES to relocate, because the recompiled bodies dereference guest addresses
directly (`RD32(a)` is `*(uint32_t *)a`). Those bases are 0x00400000 for
XMen2.exe and 0x10000000 for the DLLs -- all inside the low 4 GB.

A 64-bit Mach-O executable is linked with a `__PAGEZERO` segment of 4 GB by
default. That reservation is unmapped and unmappable, so every address the
guest image needs is unavailable for the life of the process.

## The mitigation, and its status

Link the native host with a small page zero:

    -Wl,-pagezero_size,0x1000

This is the standard approach for emulators and JITs that need low addresses on
macOS. It is NOT verified here: this machine is Linux, and nothing in this repo
has been built or run on macOS. Treat the mitigation as the thing to try first,
not as a known-good fix.

## What would falsify the concern entirely

If a macOS build with the default page zero maps 0x10000000 successfully, the
premise is wrong and this entry should be closed with that measurement. Do not
close it on reasoning.

## Knock-on

If the mitigation does not hold, identity mapping has to be abandoned on macOS,
and the memory accessors in `src/recomp/x86rt.h` need a base-offset form
(`RD32(a)` becomes `*(uint32_t *)(g_mem + a)`). That is a change to the
EMITTED code's assumptions, not just the host, so it would be far cheaper to
find out early -- which is the reason this is filed now rather than later.
