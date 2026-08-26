---
id: 10
title: macOS __PAGEZERO makes the low 4 GB unmappable, which is exactly where the guest image has to go
status: resolved
symptom: On macOS, pe_map's MAP_FIXED_NOREPLACE at 0x10000000 (or 0x400000 for XMen2.exe) fails, so x2native reports that the kernel would not place the image at its preferred base and refuses to run
tags: pc,recomp,native,macos,portability,rc-native
created: 2026-08-05
updated: 2026-08-26
---

## Resolution

Apple Silicon now runs the native arm64 build with the normal 4 GB
`__PAGEZERO`. `src/native/guest_memory.c` reserves a separate 4 GB host arena;
every logical 32-bit guest address is translated through its base, and every
host/guest boundary uses the same helpers. PE images, the heap, file mappings,
stacks, D3D8 buffers, strings and diagnostics no longer require identity maps.

The alternative was measured and rejected: an arm64 Mach-O linked with
`-Wl,-pagezero_size,0x4000` was killed before `main` (exit 137), while the
normal 4 GB page-zero binary cannot map the PE bases by either `mmap` or
`mach_vm`. This was a port assumption, not an OS limitation to work around.

The other portability blocker was `ucontext`. Guest threads are now pthreads
serialized by one mutex and parked/woken by condition variables. Nested Win32
suspend counts are preserved; quantum preemption temporarily releases the
guest mutex rather than switching C stacks.

Validation on 2026-08-26: the arm64 Mach-O passed the 93-check native battery
and the full 106-test CTest gate passed 103 with three optional-data skips.
The battery now includes two 4 KiB Windows pages in one 16 KiB Apple hardware
page and proves that decommitting one does not revoke its committed sibling.
A driven D3D8 run cleared all six intro movies, passed the former allocator
fault after `i105.sfd`, entered the playable world, enabled the world shadow
pass and continued presenting geometry for another 50 seconds. A probe-enabled
live run bound 16 of 16 generated probes through Mach-O override slots.

## Original collision

SDL3 GPU was chosen partly because it maps onto Metal on macOS. That makes
macOS a real target, and the native design has a hard assumption that collides
with it.

## The collision

The original `src/native/pe_map.c` mapped the PE at its preferred host address,
because recompiled bodies dereferenced guest addresses directly (`RD32(a)` was
`*(uint32_t *)a`). Those bases are 0x00400000 for XMen2.exe and 0x10000000 for
the DLLs -- all inside the low 4 GB.

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

The implementation chose that bounded translation cost. The generated memory
accessors and the handwritten host boundary now share the same address model.

## What would reopen it

Any raw guest-address dereference that bypasses `guest_memory_pointer`, an
arm64 run that cannot complete the native battery, or a threaded movie run that
stalls with a runnable worker unable to acquire the serialized guest turn.
