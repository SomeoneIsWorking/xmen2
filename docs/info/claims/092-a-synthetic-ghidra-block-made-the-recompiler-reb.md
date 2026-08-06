---
id: C092
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,translator,correctness
---

## Claim

A synthetic Ghidra block made the recompiler rebase BIT MASKS as if they were addresses, in 8460 operands across 2513 functions

## Evidence

tools/recomp.py computed the module image as max(start+size) over every block in the Ghidra export. That export carries a synthetic 'tdb' block (thread debug block) at 0xffdff000, so the computed end was 0xffe00000 for every module and img_rel() treated EVERY immediate from the image base up to ~4 GB as an address into this module, rewriting it as (G_IMGBASE + offset). The failure mode is not a wild pointer -- it is arithmetic silently changing value. Measured over the ten exported JSONs: 8,460 operands in 2,513 functions were wrongly rebased; worst is XMen2.exe with 6,400 in 1,976 functions, because its base is 0x400000 so nearly any large constant qualified. Concrete instance and how it was found: 'AND ECX,0x7fffffe1' at libIGCore 0x10057325 in igArena_malloc was emitted as 'AND ECX,(G_IMGBASE + 0x6fffffe1)'; with libIGCore mapped at 0x24000000 the mask became 0x93ffffe1, so the small-chunk path failed to clear the header's sign bit, the chunk kept its 12-byte extension form while ms->top advanced as if the header were 4 bytes, and the allocator returned a block overlapping its own top chunk (issue #15, C091). FIXED: image_bounds() now takes the run of blocks contiguous from the image base and RETURNS what it excluded so the caller prints it ('block NOT part of the image: tdb 0xffdff000+0x1000'), and emit reports how many immediates it rebased so a bounds regression is visible as a spike. After regenerating all eight linked modules the native run no longer faults in consolidate; it advances past the whole arena path and stops on an ordinary missing-function seed (0x100177da), which is what native_discover.sh consumes. The 33-check battery still passes 0 failures.

## What would falsify it

The contiguity rule uses 64 KB of slack between blocks. If a module ever has a real section separated from the rest by more than that, image_bounds() would exclude it and addresses into it would stop being rebased -- the opposite failure, and equally silent in the emitted C. The printed exclusion list is the guard: anything other than 'tdb' appearing there means the rule is wrong for that module.
