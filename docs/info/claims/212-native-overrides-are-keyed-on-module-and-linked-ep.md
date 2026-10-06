---
id: C212
kind: claim
status: holds
created: 2026-08-18
tags: override,jit,runtime,dispatch,collision
---

## Claim

A native override must be keyed on (module, linked entry point), never on a
bare address: keyed on the address alone, two of the 19 overrides could never
fire and one of them could fire for an unrelated module's function

## Evidence

Every `libIG*.dll` is linked for `0x10000000`, and `pe_map` relocates all but
one of them, while the dispatcher works in MAPPED addresses. So a table keyed
on the linked address is wrong in both directions, and both were real:

- `movie.c` registered `0x10002520` for libCriMovie's decoder-rendezvous
  override, but libCriMovie maps at `0x25000000`, so the dispatcher compared
  `0x25002520` and never matched. `reportbox.c`'s `0x10069c70` (libIGCore, at
  `0x2f000000`) was dead the same way. Both were silently inert: a missing
  override means the runtime-translated retail body answers, and the run can
  look healthy.
- `0x10002520` is a real function in EIGHT modules (cgD3D8, libCriMovie,
  libIGCollision, libIGCore, libIGGfx, libIGGui, libIGUtils, libIGViewer), and
  cgD3D8 keeps the preferred base `0x10000000` -- so the movie override was
  reachable for cgD3D8's unrelated function while dead for its intended target.

Fixed by making the key explicit: `x86_register_override("libCriMovie.dll",
0x10002520, fn)`. `x86_overrides_resolve()` runs after `pe_map` has placed
every module and converts each pair to the mapped address, aborting if the
module is not mapped, the address is outside its image, or it is not the entry
point of executable guest code. An override that does not resolve is rejected
before gameplay.

Verified: `overrides: 19 native override(s) resolved to mapped addresses` on a
real run (was 17 live + 2 dead); `--override-selftest` shows the resolver
ACCEPTING a real entry point and REJECTING an unmapped module, an out-of-image
address, and an invalid entry (4/4).

## What would falsify it

A registration whose module maps at its preferred base failing to fire (the
resolution arithmetic is wrong), or `x86_overrides_resolve` accepting a pair
whose module is absent, or two modules both claiming one registration again
