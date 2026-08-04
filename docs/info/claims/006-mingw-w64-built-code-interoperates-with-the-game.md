---
id: C006
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

mingw-w64-built code interoperates with the game's MSVC C++ call sites: naked asm thunks receive real MSVC __thiscall/__stdcall calls from XMen2.exe and the original DLLs and pass them through intact. The ABI risk that would have killed the per-DLL replacement strategy is retired.

## Evidence

Tracing shim (scratch/shim/trace, built by tools/gen_trace.py) replaced 22 of libIGDisplay's 24 boundary exports with pushal/pushfl -> log -> popfl/popal -> jmp *real thunks. Game ran 60s past the splash into the intro cinematic (letterboxed movie frame captured) and logged 9 calls in boot order starting igWindow::getClassTypeLazy, arkRegister, _instantiateRefFromPool, _instantiateFromPool, igWin32Window::hideCursor. No crash, no stack corruption.

## What would falsify it

This proves pass-through only. A thunk that CONSUMES arguments (rather than jumping) and gets a signature wrong would still corrupt the stack; and it says nothing about our code CONSTRUCTING an object the original libIGCore accepts, which is the next real risk.
