---
id: C276
kind: claim
status: holds
created: 2026-09-03
tags: 
depends: src/native/x86_engine_dispatch.c#x86_engine_jit_dispatch
---

## Claim

engine=jit inline dispatch (jit.inline_dispatch) cuts host-import wall-time share in-game from ~62% to ~18% and raises frames-per-fixed-window ~15%

## Evidence

A/B on identical driven input path 2026-09-03, X2_HOTEP=64 wall-time split + present counts, numbers recorded in docs/issues/0141 (scratch logs since GC'd); x86port d5d3b00 + src/native/x86_engine_dispatch.c

## What would falsify it

x86port dispatch hook removed/renamed, or a driven in-game A/B shows host-import share back above ~40% with the CVar on
