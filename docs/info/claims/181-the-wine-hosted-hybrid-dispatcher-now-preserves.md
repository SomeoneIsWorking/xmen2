---
id: C181
kind: claim
status: holds
created: 2026-08-14
tags: pc,recomp,hybrid,abi
---

## Claim

The Wine-hosted hybrid dispatcher now preserves tail-call ordering across direct generated calls: a nested virtual tail target finishes before its caller resumes, while a same-level tail chain remains iterative.

## Evidence

Issue #68. tools/recomp_hosted.py keys pending tails to CPU.call_depth/dispatch_depth; tools/recomp.py emits X86_TAIL_FN on every generated tail exit. WATCH=1 x2run: FUN_005c7a00 entered at ESP 0x039eecd4 and returned at 0x039eecd8; MSVCR71 __security_error_handler was absent; the wrapper remained alive after 15 s and captured frames had 593/1098 colors. tests/test_recomp_hosted.py and tests/test_recomp.py pass.

## What would falsify it

A generated direct caller resumes before a nested tail target completes, any generated tail path leaves CPU.call_depth unbalanced, or a current x2run run reaches __security_error_handler from a balanced original function.
