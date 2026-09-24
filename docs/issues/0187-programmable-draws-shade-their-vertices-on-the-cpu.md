---
id: 187
title: programmable draws shade their vertices on the CPU, every draw
status: open
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
