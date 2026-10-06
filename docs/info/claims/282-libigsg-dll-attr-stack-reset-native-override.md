---
id: C282
kind: claim
status: holds
created: 2026-09-04
tags: pc,native,jit,performance,libIGSg,attr-stack
depends: src/native/attr_stack.c#x2_override_10034d30, src/native/attr_stack.c#x2_override_10034d10, src/native/attr_stack_verify.c#attr_stack_verify_end
---

## Claim

Native overrides for `igAttrStackManager::reset` (`libIGSg.dll!0x10034d30`) and `igAttrStack::customReset` (`libIGSg.dll!0x10034d10`) eliminate ~3.26 million inner-loop function call/return frames and JIT block entries per 2000 in-game frames, reducing average frame time from 16.62 ms to 15.14 ms (-8.9%) and improving present framerate from 57.4 FPS to 62.7 FPS (+9.2%).

## Evidence

In a 2000-frame in-game profiling run (`act0/tutorial/tutorial1`, `jit.profile=65536`), `igAttrStack::customReset` at `0x10034d10` was the single most frequently executed block in `libIGSg.dll`, entering 3,260,000 times (accounting for 9.8M JIT block transitions and ~2.4% of total guest wall time). Disassembly confirmed that `igAttrStackManager::reset` loops over all active attribute stacks and executes a direct call to `customReset` (`0x10034d10`) for each element, before invoking `clearLightHandles` (`0x10035950`) and clearing manager pointers.

`src/native/attr_stack.{c,h}` provides native implementations:
- `attr_stack_custom_reset`: resets stack offsets `+0x08`, `+0x18`, `+0x20`, `+0x24`, `+0x28`, and `+0x30`.
- `x2_override_10034d10`: native override for `0x10034d10` popping `ret` (`C->esp += 4u`).
- `x2_override_10034d30`: runs the attribute stack reset loop natively in host C, clears manager pointers, invokes `clearLightHandles` via `x86_guest_call_args`, and pops `ret` (`C->esp += 4u`).

Controlled by runtime CVar `sg.attr_stack` (default true) for runtime A/B
toggling without rebuilding. A differential verification harness in
`src/native/attr_stack_verify.{c,h}` (gated on `sg.attr_stack_verify=1`)
snapshots the initial state of all active attribute stacks and manager pointers,
executes the native override, restores initial values, executes the guest body
via `x86_guest_body`, and asserts bit-for-bit equivalence across all fields.
Verified across 1,740 in-game frames with zero assertion divergences.

Unit tested in `tests/test_attr_stack.c` (test #76) covering all struct offsets, multi-stack loops, pointer resets, and stack balance (`C.esp`).

Measured in a 2000-frame unpaced in-game benchmark (`act0/tutorial/tutorial1`, `X2_UNPACED=1`):
- With guest execution (`--set sg.attr_stack=0`): 2005 frames in 34.95s, avg frame time 16.62 ms, p50 3.29 ms, 57.4 FPS.
- With native override (`--set sg.attr_stack=1`): 2004 frames in 31.95s, avg frame time 15.14 ms, p50 2.92 ms, 62.7 FPS.
- Net improvement: -1.48 ms per frame (-8.9%), +5.3 FPS (+9.2%), 3.0s wall time reduction over 2000 frames.

## What would falsify it

Any mismatch between native and guest execution detected by `sg.attr_stack_verify=1`, any stack pointer imbalance after returning from either override, or a regression in `ctest`.
