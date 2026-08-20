---
id: C232
kind: claim
status: holds
created: 2026-08-21
tags: recomp,x87,clang
depends: src/recomp/x87host.c#x87_host_end, tests/test_x87host.c
---

## Claim

The ordinary-host-call x87 bridge captures ST0 correctly under Clang because it drains the hardware stack before any compiler-generated C floating-point operation

## Evidence

Clang disassembly before issue #93 showed fldz above the fstpt drain and test_x87host returned depth=1,value=0; after removing the redundant C long-double initialization, test_x87host and the complete 52-test Clang CTest suite pass

## What would falsify it

Clang disassembly inserts any x87 operation between fxsave/tag inspection and the first fstpt, test_x87host captures a value other than 1.0, or a real ordinary host float return diverges from the stock call
