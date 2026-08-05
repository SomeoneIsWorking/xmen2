---
id: 6
title: The discovery loop seeded jump-table targets as functions, and the boot got further while being more wrong
status: resolved
symptom: After a real fix, the run reports a new 'unresolved indirect call' whose address is a few bytes past a CRT function; seeding it makes that stop and the run proceed, then crash elsewhere with impossible state (a C++ 'this' pointing into the stack, a count of 4066936, a base of 0)
tags: xbox,recomp,discovery-loop,jump-table,no-hacks
created: 2026-08-05
updated: 2026-08-05
---

## Symptom

The carry-flag fix (issue #5) cleared the old blocker and the boot took a new
path, ending at `UNRESOLVED VA 0x003D5B54`. The discovery loop did what it
always does: seeded it, re-lifted, ran again, found 0x003D5B88, then 0x003D5B44,
then reported `DONE -- every indirect call resolved`.

It was not done. The run then died inside the name-table binary search with
`this=0x00F7FF38` (a **stack** address), `count=4066936`, `base=0`.

## Cause

Those three addresses are not functions. They are mid-function instructions in
memcpy's unrolled copy tail:

    0x003D5B44  mov eax, dword ptr [esi + ecx*4 + 0x10]
    0x003D5B54  mov eax, dword ptr [esi + ecx*4 + 8]
    0x003D5B88  mov eax, dword ptr [ebp + 8]

They are **jump-table targets**. `sub_003D5890` (memcpy) dispatches through
NINE tables; only two were enumerated, so the rest fell back to
`RECOMP_ITAIL` -- an indirect tail JUMP.

And `RECOMP_ITAIL` logged through `recomp_icall_fail_log`, the same path as a
failed indirect CALL. So a translator gap was indistinguishable from a missing
function, and the loop "fixed" it by inventing three functions with no
prologue. Each dispatch then resolved, the fragment executed on the caller's
frame, and the run carried on corrupted. **The symptom moved and the port got
further while being more wrong** -- the exact failure the no-hacks rule exists
to prevent.

## Fix (three layers, so it cannot recur quietly)

1. **Say which it is.** `RECOMP_ITAIL` now calls `recomp_itail_fail_log`, which
   reports `UNRESOLVED-TAIL-JUMP VA 0x...` and states that the address is a
   switch-table target, not a missing function, and must not be seeded. The
   end-of-run summary calls it `unenumerated switch table -- NOT a seed
   candidate` instead of `unresolved (seed candidate)`. The kind is in the
   grepped line because tooling reads it.
2. **The loop refuses.** `xbox_discover.sh` checks for a tail-jump miss BEFORE
   its seed-candidate grep and stops with the reason and the real fix. It also
   refuses any address that falls inside an already-detected function.
3. **Read the tables properly.** `_read_jump_table` now stops at the first entry
   that leaves the function instead of over-running the table end and having
   the caller discard everything; gotos are emitted only for targets that
   exist in this body.

## What is still open

memcpy's later tables STILL fall back, because the detector under-sizes the
function: memcpy is detected as `0x003D5890-0x003D5A7F` while its own switch
targets live at `0x003D5B44+`. That is C047. Tracked by
`test_memcpys_later_switch_tables_are_enumerated`, marked
`xfail(strict=True)` so the suite fails the moment the boundary is fixed and
the marker is stale.

The difference from before: the run now STOPS at the gap and names it, instead
of being talked past it.
