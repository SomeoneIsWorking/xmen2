---
id: C072
kind: claim
status: holds
created: 2026-08-05
tags: xbox
---

## Claim

The lifter computed an indirect call's target AFTER pushing the dummy return address, so every esp-relative call target was 4 bytes off. On real x86 the call computes its memory operand from the esp it was reached with and pushes the return address afterwards. The target is now snapshotted into _icall_tgt before the dummy push, inside the same block that declares _icall_esp so two calls in one function cannot collide.

## Evidence

The original bytes at 0x00284FE0 are 57 FF 54 24 08: push edi; call dword ptr [esp+8]. The lifted form emitted PUSH32(esp, 0) and then evaluated MEM32(esp + 8), reading four bytes below the function pointer -- a 0 -- so the runtime reported an out-of-image indirect call to 0x00000000 and the call did not happen. A gdb backtrace at recomp_icall_range_skip_log named the site: sub_00284FE0 <- sub_002AC4F0 <- sub_002AFBB0 <- sub_000132F0 <- sub_00225995 <- sub_0022286B. Two new tests in test_lifter.py fail on the old lifter and pass on the new one (run against both). After re-lifting: 40 esp-relative indirect-call targets across the title, the NULL call is gone, and the run reaches 8719 indirect calls, up from 7724.

## What would falsify it

if any of the other 39 esp-relative sites were RELYING on the off-by-four (they cannot be, but the count is the thing to re-check), the re-lift would show new unresolved targets rather than fewer
