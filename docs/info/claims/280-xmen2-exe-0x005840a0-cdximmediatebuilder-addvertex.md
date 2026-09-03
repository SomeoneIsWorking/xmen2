---
id: C280
kind: claim
status: holds
created: 2026-09-03
tags: pc,native,jit,performance,graphics,overrides
depends: src/native/vertex_builder.c, src/native/vertex_builder_verify.c
---

## Claim

XMen2.exe!0x005840a0 (CDxImmediateBuilder::addVertex) is natively owned, eliminating ~15% of in-game JIT block dispatches, and is bit-exact vs the guest body.

## Evidence

In-game block-entry profiling (jit.profile=65536) identified 0x005840a0 and its nested cross-module vector assignments (libIGMath.dll!??4igVec3f and ??4igVec2f) as the hottest cluster in the game: 6 blocks in XMen2.exe (0x005840a0..0x005840ff) plus the 2 vector assignments, each called ~2.62M times per 1000 frames (20.9M block entries, 15.0% of all JIT execution).

src/native/vertex_builder.c registers a fully-replacing native override implementing the exact append contract: Vec3f position copy, optional Vec2f UV copy, 32-bit color store, stride increments, capacity limit, and ret 0xc cleanup.

src/native/vertex_builder_verify.c + `--set gfx.vtx_builder_verify=1` snapshots the builder state, runs the native override, re-runs the guest body from the same initial state, and verifies bit-for-bit equivalence on memory writes, count, and registers. Driven in-game for 500 presented frames with verify enabled: 0 disagreements, clean completion.

With the override active, all 8 top blocks (0x005840a0..0x005840ff, ??4igVec3f, ??4igVec2f) completely leave the jit.profile top 40. Total block entries across a matched 1000-frame run dropped from 139,589,046 to 116,373,913 (23.2 million fewer block entries, a 16.6% reduction).

test_vertex_builder exercises with-UV, without-UV, capacity-limit, stack delta, and constructor registration (127/127 ctest pass).

## What would falsify it

gfx.vtx_builder_verify aborts in a driven session, or 0x005840a0 reappears in jit.profile with the override registered, or immediate geometry/text/HUD renders with misplaced vertices or corrupted colours.
