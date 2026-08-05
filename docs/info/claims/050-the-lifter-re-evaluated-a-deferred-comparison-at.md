---
id: C050
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: patches/xboxrecomp/0003-recompiler-jump-tables-and-loud-drops.patch
---

## Claim

The lifter re-evaluated a deferred comparison at the jcc instead of at the cmp, so any instruction between them that reassigned an operand silently flipped the branch. 216 such pairs existed in this title's lift.

## Evidence

Static scan of the generated C for 'cmp X, Y - flags set for next jcc' followed by an assignment to X or Y before the condition: 216 of 8602 deferred sites. Snapshotting the operand values into _flg0/_flg1 at the flag-setting instruction takes it to 0 real cases (10 residual hits are the adjacent cmp+jcc form, which the scan cannot distinguish). In the run it removes the corruption directly: the indirect call to the stack address 0x00F7FF30 -- which came from a red-black-tree walk passing its 0x3FFFFFFF nil sentinel to an index-to-node accessor and writing over a live vtable pointer -- no longer happens, and the run goes from 3632 to 3971 indirect calls.

## What would falsify it

flags set by an instruction whose operands are memory that changes between setter and consumer are captured by value here; if any consumer needs the LIVE value rather than the flag-time value, this is wrong for that case
