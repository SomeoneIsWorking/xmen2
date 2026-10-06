---
id: 189
title: Adreno's shader compiler crashed on the VS 1.1 interpreter at New Game
status: resolved
symptom: on an Adreno 722 phone, New Game exits at the loading screen with SIGSEGV at 0x48 in /vendor/lib64/libllvm-qgl.so
state_items: S018
tags: android,vulkan,adreno,shaders,crash
created: 2026-09-25
updated: 2026-09-25
---

# 0189 — Adreno's shader compiler crashed on the VS 1.1 interpreter at New Game

## Observed

On an HONOR 600 (VKJ-NX9, Android 16, Adreno 722, Vulkan driver
`0x8032004a`), v0.2.8 exited during New Game's loading screen. The phone
recorded `EXIT_SELF status=3`, which is `fault_report.c`'s `_exit(3)`. The
fault report named the host PC as `libllvm-qgl.so+0x4c0a34`. That library is
Qualcomm's LLVM-based shader compiler. The fault came right after the first
`CreateVertexShader` (104 bytecode dwords), when the pipeline for the first
programmable draw was built. The renderer had not changed since v0.2.6, so
every release since the GPU interpreter landed (#187) crashed on this driver.

The phone keeps only error-level logcat lines (`persist.log.tag=E`), and the
port logs at INFO. To read the report, run
`adb shell setprop log.tag.x2native V` before the run.

## Cause

This is a null dereference inside the driver's optimizer. It is not in our
code. The faulting helper follows `arg->[0x48]->[0x48]` and reads `[0x38]`,
the parent-function slot of an LLVM `BasicBlock`. It receives a null argument
from a pass that walks instruction users and branches on terminator opcodes
(caller `+0x72ed08`). The library is stripped, so the pass cannot be named.

A standalone probe on the phone built one vertex-only pipeline per SPIR-V
module, with the same glslc flags as the build. The results were
deterministic, three runs each:

- These crash: `d3d8_vs11.vert` and `shadow_vs11.vert`, and a minimal
  `vs11_run()` wrapper whose only output is `gl_Position`.
- These compile: `d3d8_fixed.vert`, and every piece of the interpreter on its
  own (constant reads, program reads, register-file indexing, input loads,
  `vs11_source`, the opcode chain).
- The trigger is how the register file starts. The crashing form zeroes it in
  a loop, stores `oD0 = 1` as a separate statement, and then indexes it
  dynamically in the program loop. With the separate `oD0` store removed,
  the minimal wrapper compiles; with it restored, the wrapper crashes.
  Replacing the runtime vector indexing (`v[s & 3u]`), a fixed write
  target, and a fixed loop count all left the crash in place.

## Fix

`vs11_program.glsl` now sets every starting value in one loop:
`vs11_reg[i] = vec4(i == VS11_OUT_D0 ? 1.0 : 0.0)`. The result is the same;
the separate store is gone. On the phone, all three shaders that include the
interpreter now compile. On desktop, the D3D8 selftest still finds that the
GPU program and the CPU executor draw the same pixels.

## Verified on the device

The CI-signed v0.2.9 APK was installed over v0.2.8 on the same phone. New
Game → Loading created the same 104-dword vertex shader, built its pipeline
and drew the first level, with no fault in the run. The frame wall averaged
25 ms over about 4,500 intervals.
