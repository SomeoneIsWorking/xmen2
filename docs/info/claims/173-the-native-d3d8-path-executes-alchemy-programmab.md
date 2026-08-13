---
id: C173
kind: claim
status: holds
created: 2026-08-14
tags: d3d8,shader
---

## Claim

The native D3D8 path executes Alchemy programmable VS 1.1 skinning after the opening conversation.

## Evidence

src/d3d8/d3d8_vertex_shader.c d3d8_vs_execute and src/d3d8/d3d8_drawcall.c d3d8_build_draw. scratch/logs/vs-execute-3350.log: one 104-DWORD shader created, 50 programmable draws and 3250 vertex invocations by frame 3350, with zero GPU refusals. d3d8_vs_selftest exercises a relative-addressed DP4 program and unsupported-opcode negative through the shipping executor.

## What would falsify it

A gameplay route binds a shader declaration/opcode the interpreter refuses, a reference comparison shows its vertex outputs differ, or changes to d3d8_vs_execute/d3d8_build_draw invalidate this run.
