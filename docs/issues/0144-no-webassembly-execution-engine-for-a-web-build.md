---
id: 144
title: No WebAssembly execution engine for a web build
status: open
symptom: nothing lowers a guest block to WebAssembly; the encoder exists, the backend does not
tags: web,wasm,jit,x86port,blocker
created: 2026-09-07
updated: 2026-09-07
---

## Causes and ownership

`shared/x86port` has two JIT backends and chooses one by host architecture:
`CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$"` selects `jit_arm64.c`,
and everything else selects `jit_x64.c`. The selection is a two-way branch with
no third arm and no refusal, because until now there were only two hosts.

Emscripten reports neither architecture, so it takes the `else()` and links the
**x86-64** emitter. That backend writes x86-64 machine code into a buffer and
transfers control to it. WebAssembly has no such operation. The configure step
would succeed, the link would succeed, and the product would be dead at the
first block it translated — the worst shape a failure can take here, because
nothing in the build says anything is wrong.

Ownership is upstream in `shared/x86port`, not in this port: x86 decode,
semantics, host emission and block-cache policy belong there, and a title-local
workaround would be exactly the shared-code split this tree exists to end.

Two candidate resolutions were considered; only one survives.

- *Select the interpreter on wasm.* Refused on two independent grounds. The
  execution-architecture guardrail says the gameplay target always uses the JIT
  and that product configuration cannot choose otherwise; and a 2005 3D action
  game under an x86 interpreter in a browser would not be playable, so it would
  ship a smaller product that merely looks like progress.
- *Add a third backend.* `emit_wasm.c` + `jit_wasm.c` translating a guest block
  into a WebAssembly module and instantiating it, implementing the same
  `x86p_jit_*` contract the other two implement.

Two browser facts constrain that backend and are design inputs, not later
tuning: synchronous module compilation is only unrestricted off the main thread
(which is where the guest loop has to run anyway, since it blocks), and
instantiated modules are permanent, so block-cache eviction becomes a
memory-correctness requirement and blocks want batching into larger modules.

An adjacent, smaller defect in the same selection: the two-way branch should
refuse an unknown host architecture by name instead of silently assuming x86-64.
A host that is neither arm64 nor x86-64 is currently mis-served in silence.

## Observation and verification

Measured, not only read. Emscripten 4.0.16 configured and built the real
projects: `jitcommon/src/jitcommon/code_memory.cpp` fails with
`fatal error: error in backend: llvm.clear_cache is not supported on wasm`.
That file is the executable code region, so the blocker is a compile error and
not an opinion.

The same run gives the denominator on the other side: 33 of 34
`x86port_runtime` translation units, 18 of 18 Zydis decoder units, 13 of 13
Zycore units (with `ZYAN_NO_LIBC`), and 233 of 233 Bochs software-float units
compile to wasm32. Decode, semantics and software math are already portable;
only machine-code emission and execution are not.

The one other failure found is separate and now tracked as gate W5 in
`docs/web-release.md`: `x86port/src/x86port/x87.c` uses `#pragma FENV_ACCESS`,
which wasm refuses because it has no floating-point environment at all, so a
wasm build must route guest x87 through the software float path.

`tools/wasm_portability.py` keeps this measured rather than remembered.

## What has landed since

Two of the three parts named above are done, upstream in `shared/x86port`
(pinned here at `34d2d95`):

- **The silent misclassification is gone.** The backend selection now names an
  unsupported host and refuses, instead of treating every non-ARM64 processor as
  x86-64. This port's portability measurement passes an explicitly named
  `-DX86P_MEASURE_UNRUNNABLE_BACKEND=ON` and is warned on every configure that
  the resulting library cannot execute a guest instruction.
- **`emit_wasm.{h,c}` writes the WebAssembly binary format**, with the same
  sticky-overflow discipline as the other two encoders plus counted size slots
  and counted control regions, because an unbalanced body or an unclosed size is
  a module an engine rejects wholesale rather than a subtly wrong instruction.
  Its oracle is a real engine: twelve modules validated and ran under node,
  covering arithmetic, five-byte constants, sign/zero-extending memory access,
  structured control, direct and indirect calls, 64-bit widening, and a trap.
  Both halves were confirmed to fire by mutation.

**Still open, and still the blocker:** `jit_wasm.c` — the lowering from a guest
basic block to a module — does not exist, so no guest instruction has executed
on a WebAssembly host. Module lifetime belongs to that work rather than after
it: an instantiated module is permanent, so per-block modules leak without
bound and block-cache eviction becomes a memory-correctness requirement.
`shared/x86port`'s `docs/migration.md` Gate 8 holds the ordered remainder.

Verification when it lands: `jit_wasm` translates and runs x86port's own JIT
suite under node, reporting translated blocks, hand-backs and refusals with
their denominators — the same counters every other backend owes.

Related: `docs/web-release.md` (gate W1), `docs/project-state.md` S021.
