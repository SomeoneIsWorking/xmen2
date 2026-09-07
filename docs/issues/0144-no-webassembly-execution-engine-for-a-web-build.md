---
id: 144
title: No WebAssembly execution engine for a web build
status: open
symptom: a guest block now lowers to a WebAssembly module and runs in an engine, but nothing in a browser instantiates or enters one
tags: web,wasm,jit,x86port,blocker
created: 2026-09-07
updated: 2026-09-07
---

## Causes and ownership

This section records the cause as it stood when the issue was opened; "What has
landed since" below says what is true now.

`shared/x86port` had two JIT backends and chose one by host architecture:
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
A host that was neither arm64 nor x86-64 was mis-served in silence. Fixed.

## Observation and verification

Measured, not only read. Emscripten 4.0.16 configured and built the real
projects: `jitcommon/src/jitcommon/code_memory.cpp` fails with
`fatal error: error in backend: llvm.clear_cache is not supported on wasm`.
That file is the executable code region, so the blocker is a compile error and
not an opinion.

The same run gives the denominator on the other side, as first measured: 33 of
34 `x86port_runtime` translation units, 18 of 18 Zydis decoder units, 13 of 13
Zycore units (with `ZYAN_NO_LIBC`), and 233 of 233 Bochs software-float units
compile to wasm32. Decode, semantics and software math are already portable;
only machine-code emission and execution are not.

The one other failure found is separate and now tracked as gate W5 in
`docs/web-release.md`: `x86port/src/x86port/x87.c` uses `#pragma FENV_ACCESS`,
which wasm refuses because it has no floating-point environment at all, so a
wasm build must route guest x87 through the software float path.

`tools/wasm_portability.py` keeps this measured rather than remembered.

## What has landed since

All three parts named above are done, upstream in `shared/x86port` (pinned here
at `e1522b2`):

- **The silent misclassification is gone.** The backend selection names an
  unsupported host and refuses, instead of treating every non-ARM64 processor as
  x86-64. Emscripten is asked about *before* the processor is looked at —
  because Emscripten reports its processor as `x86`, which is how it got the
  x86-64 emitter in the first place — so it now selects the WebAssembly
  backend. The
  `-DX86P_MEASURE_UNRUNNABLE_BACKEND=ON` this port's measurement used to pass is
  gone from `tools/wasm_portability.py`; the option survives upstream for a host
  that genuinely has no backend.
- **`emit_wasm.{h,c}` writes the WebAssembly binary format**, with the same
  sticky-overflow discipline as the other two encoders plus counted size slots
  and counted control regions, because an unbalanced body or an unclosed size is
  a module an engine rejects wholesale rather than a subtly wrong instruction.
  Its oracle is a real engine: twelve modules validated and ran under node,
  covering arithmetic, five-byte constants, sign/zero-extending memory access,
  structured control, direct and indirect calls, 64-bit widening, and a trap.
  Both halves were confirmed to fire by mutation.
- **The lowering exists and is engine-verified.** `jit_wasm_lower.c` plus five
  per-family units turn a guest basic block into a module for a named
  instruction subset: MOV/MOVZX/MOVSX, LEA, XCHG, SETcc, PUSH/POP/LEAVE,
  CDQ/CWDE, CLD/STD, the inline ALU shapes (ADD, SUB, CMP, OR, AND, TEST, XOR,
  NOT), the helper-backed ones (ADC, SBB, NEG, INC, DEC, every shift and rotate)
  and the branches (JMP, Jcc, JECXZ, CALL, RET). `test_jit_wasm` translates 38
  blocks, hands each to node, and compares the whole `X86pCpu` and all of guest
  memory against the separately linked interpreter: **38 of 38 blocks reached
  the engine, 1,097 checks, 0 divergences.** The four helper functions are
  *recorded, not reimplemented* — C calls the real `x86p_alu`, `x86p_alu_unary`,
  `x86p_cond` and `x86p_flag_cf` on the pre-call state and hands the oracle the
  return value and the exact bytes written, which the oracle replays. Thirteen
  deliberate mutations were run; twelve were caught, four of them only after
  cases were added for them, and the thirteenth is recorded as unobservable at
  the site rather than quietly dropped.
- **Module lifetime is owned, not deferred.** `jit_wasm_arena.c` holds published
  modules against an `X86pWasmHost` interface, caps live modules at 1,024, and
  **refuses by name past the cap rather than evicting** — because the block
  cache holds entry addresses the arena handed out and does not consult it
  before entering one, so evicting behind its back would be a use-after-free.
  Cap refusals are counted apart from engine rejections. 32 checks through a
  stub engine.
- **It compiles for the target.** All ten WebAssembly backend files compile to
  wasm32 under Emscripten's own clang with `-Wall -Wextra -Werror` and no
  warnings, and no x86-64 emitter object appears in that build. That is the
  first time any of it has been through a real wasm32 compiler, and it is what
  makes `jit_wasm.c`'s `_Static_assert(sizeof(void *) == 4)` a checked fact.

**Still open, and still a blocker.** No guest instruction has executed in a
browser, and the remaining work is not a detail:

1. **Nothing instantiates a module.** `X86pWasmHost` has no implementation; the
   only thing that has ever implemented it is a test stub. The real one is
   JavaScript glue plus an Emscripten boundary, and a resolved export has to
   come back as an indirect-table index, which is what an Emscripten function
   pointer is.
2. **The dispatcher has no publication edge.** `jit_engine.c` translates into
   the executable code region and enters it; the wasm path produces a module and
   an export name instead, and that fork does not exist. This is why
   `code_memory.cpp` still fails to compile for wasm32 and why that failure is
   the right thing to keep measuring.
3. **The instruction set is a first slice.** No string operations, no
   multiply/divide, no SIMD, no x87, no `LOOP`, no double shifts — each of which
   the x64 backend has and the guest uses. Anything outside the slice is a named
   refusal, not a wrong answer.
4. **Compilation must move off the main thread**, where synchronous module
   compilation is unrestricted. That is the same place gate W3 puts the guest
   loop, so W1 and W3 land together.
5. **Eviction is absent on purpose.** A long session reaches the 1,024-module
   cap and is refused. Closing that needs the block cache to tell the arena when
   it discards a block, and blocks to be batched into larger modules — the
   module builder already takes its function count up front so that several
   blocks can share one.

`shared/x86port`'s `docs/migration.md` Gate 8 holds the ordered remainder.

Verification when it closes: an Emscripten build of this port reporting a
nonzero count of translated blocks executed in the browser, with hand-backs and
refusals against their denominators — the same counters every other backend
owes. The engine-verified test suite above is evidence in hand and is no longer
the thing being asked for.

Related: `docs/web-release.md` (gate W1), `docs/project-state.md` S021.
