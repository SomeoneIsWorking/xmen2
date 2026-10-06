---
id: 187
title: programmable draws shade their vertices on the CPU, every draw
status: closed
symptom: the browser's guest worker spends about 1.7% in d3d8_vs_execute and 1.4% in d3d8_build_draw_impl, nearly all of it for DrawIndexedPrimitive with the one skinning shader
state_items: S010, S021
tags: d3d8,vertex-shader,performance,web,rendering
created: 2026-09-24
updated: 2026-09-24
---

# 0187 — programmable draws shade their vertices on the CPU, every draw

## Observation

On `#test-play` (10,119,323-byte wasm, 15 s profile of the guest worker):

- `d3d8_vs_execute` has 950 samples, 943 of them reached through
  `d3d8_build_draw_impl` from `dev_DrawIndexedPrimitive`.
- `d3d8_build_draw_impl` has 818 samples of its own.

Each programmable draw runs the guest's VS 1.1 program (#64) on the host CPU,
into a `malloc`'d array. That array goes into a GPU vertex buffer created,
uploaded and destroyed for that one draw.

## Measured since: the executor is vectorised

The web build now compiles with WebAssembly SIMD. That halves the executor's
cost per vertex: 958 samples at about 96K vertices a second became 602 at
about 108K (`[HB] VS 1.1 executor` gives the vertex rate). The per-draw
buffer, its upload, and `d3d8_build_draw_impl`'s own 1.6% are unchanged, and
the GPU program below still removes all of them.

## Ruled out: shading only the vertices a draw reaches

A draw shades every vertex its bound buffer holds, not the range its indices
reach. Restricting it to that range was built and measured on the native
Dead Zone route (headless, 25 s after the opening dialog):

`draws shaded 10040466 vertices of the 10040466 their buffers held`

Every skinned draw already reaches its whole buffer. The index scan would be
pure cost, so the change was not kept.

## Proper fix

Run VS 1.1 on the GPU. Translate the decoded program (`d3d8_vs_decode.cpp`
already produces it) into the host shader language once, at
`CreateVertexShader`. Upload the constant file as a uniform block per draw, and
bind the guest's vertex buffer directly. This removes the CPU pass, the
per-draw buffer and its upload. The CPU executor stays as the reference the
translated shader is differentially tested against.

## Resolution

VS 1.1 runs on the GPU. `d3d8_vs_gpu.cpp` packs the decoded program once per
shader into `GpuVsProgram` (`src/gpu/gpu_vs_program.h`). `d3d8_vs_draw.c`
binds the guest's vertex buffer and hands the program and the device's
constant file to the draw. `gpu_vertex_uniforms.c` and `gpu_shadow.c` push
them to `vs11_program.glsl`, which is included by `d3d8_vs11.vert` (the scene)
and `shadow_vs11.vert` (the caster). It interprets the program as uniform
data, so no shader is compiled at run time. Programs with an input past v15 or
a SHORT2/SHORT4 input keep the CPU executor, and the log names them once.

Differential proof:

- `d3d8_vs_gpu_selftest.c` (`--d3d8-selftest`) draws one program through the
  production draw builder on each executor and needs identical pixels. The
  program covers relative addressing from a UBYTE4, every opcode, negation,
  swizzles, partial masks and all three outputs. Ten shader mutants fail it.
- `gpu_shadow_selftest.c` needs a programmable caster to shadow the same
  pixels as a fixed one. Two mutants of `shadow_vs11.vert` fail it.

On `#test-play` (10,068,268-byte wasm) the heartbeat shows 20,118
programmable draws on the GPU and none on the executor.
`d3d8_vs_execute` is gone from the profile, and `d3d8_build_draw_impl` fell
from 1.6% to 0.87% of the worker.
