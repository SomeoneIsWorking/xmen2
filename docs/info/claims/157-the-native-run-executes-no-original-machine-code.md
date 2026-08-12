---
id: C157
kind: claim
status: holds
created: 2026-08-12
tags: recomp
---

## Claim

The native run executes NO original machine code: every instruction comes from the translator.

## Evidence

x86_allow_fallback is set only in src/app/x2run.c (the Wine front end) and is 0 in the native build; x86_note_fallback aborts by name rather than running the original image. A run that reaches its shutdown report therefore never took the hybrid path, and the report now says so on every run.

## What would falsify it

anything setting x86_allow_fallback in the native build, or a run that aborts in x86_note_fallback -- both would mean a dispatched target had no recompiled body, which is the rc-hybrid debt reappearing on this path
