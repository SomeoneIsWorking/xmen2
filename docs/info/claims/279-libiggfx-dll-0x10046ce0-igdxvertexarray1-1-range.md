---
id: C279
kind: claim
status: holds
created: 2026-09-03
tags: 
depends: src/native/vertex_color_swizzle.c, src/native/vertex_color_swizzle_verify.c
---

## Claim

libIGGfx.dll!0x10046ce0 (igDxVertexArray1_1 range colour swap) is natively owned and bit-exact vs the guest body

## Evidence

test_vertex_color_swizzle checks the override against an independent byte-level swap reference (colour range, flag-bit tail, non-colour type, ret-8 esp). Driven in-game with --set gfx.vtx_swizzle_verify=1: first colour-path swap of a 122884-byte vertex buffer matched the guest body, 0 disagreements. With verify off, 0x10046cxx / 0x2b046cfa leaves the jit.profile top-40 (was block #1 at 4.4%). Gameplay renders with correct colours.

## What would falsify it

gfx.vtx_swizzle_verify aborts in a driven session, or 0x2b046cfa reappears in jit.profile with the override registered, or a retail revision changes the igDxVertexArray1_1 vtable slot / field offsets (this+0x38 stride, this+0x3b colour offset, this+0x60/0x68 flags)
