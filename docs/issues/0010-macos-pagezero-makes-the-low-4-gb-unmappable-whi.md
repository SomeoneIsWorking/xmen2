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

## Correction: this is a choice, not a constraint

The first version of this entry called `-pagezero_size` "the standard approach
for emulators that need low addresses", which understated our position. An
emulator is stuck with identity mapping because it cannot change how the guest
addresses memory. We generate the accessors, so we can.

`RD32(a)` is `*(uint32_t *)a` only because the PC recomp began as a DLL swap,
where the guest and host really were one address space and a guest pointer WAS
a host pointer. In a native build nothing forces that. A base-offset form --
`*(uint32_t *)(g_mem + a)` -- removes the low-4-GB requirement completely, on
macOS and everywhere else, and costs an add per access that the compiler will
usually fold into the addressing mode.

What identity mapping actually buys, and what base-offset would therefore cost,
is POINTER TRANSLATION AT THE HOST BOUNDARY: today a guest string handed to a
host `printf`, or a guest buffer handed to SDL, needs no conversion. Under
base-offset every pointer crossing that boundary has to be translated. Since we
are writing that boundary ourselves anyway (43 Win32 calls plus SDL), the
translation belongs there and is bounded -- it is a cost, not a blocker.

So the decision to make is which of the two to pay for, and it should be made on
the boundary's shape rather than on macOS's page zero. Deferring it is fine
while the boundary is small; it gets more expensive the more of it exists.

## The mitigation, and its status

Link the native host with a small page zero:

    -Wl,-pagezero_size,0x1000

This is the standard approach for anything that needs fixed low addresses on
macOS. Nothing here interprets or emulates instructions -- the guest code was
statically recompiled to C and runs as native code; what it needs the low 4 GB
for is that its DATA addresses are baked into that C. It is NOT verified here: this machine is Linux, and nothing in this repo
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
