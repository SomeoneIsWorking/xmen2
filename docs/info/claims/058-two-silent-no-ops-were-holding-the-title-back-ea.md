---
id: C058
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: patches/xboxrecomp/0002-kernel-handle-and-ordinal-tables.patch, patches/xboxrecomp/0003-recompiler-jump-tables-and-loud-drops.patch
---

## Claim

Two silent no-ops were holding the title back, each found by making silence impossible: the 277 unresolved-call stubs were EMPTY (a direct call did nothing and the caller read a stale eax as the return value), and PsCreateSystemThreadEx handed back the constant 0xBEEF0001 as a thread handle, which the title dereferences.

## Evidence

Giving each stub a body that reports itself named 0x0010C470 on the first run; seeding it took indirect calls from 4157 to 7715. Backing the thread handle with a real zeroed 0x200-byte guest object instead of the constant took kernel calls from 538 to 1050 and indirect calls to 8228 -- the previous crash was a read of Xbox VA 0xBEEF0001 with esi holding exactly that constant. The stub tally now prints 'none of the 276 empty stubs was called' when clean, so its silence is a statement.

## What would falsify it

the thread object is zeroed memory, not a KTHREAD: any field the title reads from it is zero, which is wrong in a way that will surface as behaviour rather than a fault
