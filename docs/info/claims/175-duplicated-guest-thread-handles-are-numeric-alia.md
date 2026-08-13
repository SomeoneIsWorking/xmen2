---
id: C175
kind: claim
status: holds
created: 2026-08-14
tags: threads,win32,movie
---

## Claim

Duplicated guest thread handles are numeric aliases of one GuestThread object, so SuspendThread, ResumeThread, waits, completion signalling, and closing either alias operate on shared thread state.

## Evidence

Static RE: libIGCore FUN_10075400 calls GetCurrentThread then DuplicateHandle at 0x10075478 and stores the real handle used by libCriMovie. Before the fix smoke_loop stalled with decoder suspended and ResumeThread(0x24) naming no guest thread. kernel32_thread_alias_selftest drives the shipping handle table and a deliberate cleared thread_rec makes it fail. After the fix smoke_loop reached frame 4200, fired all 6 inputs, refused 0 draws, and emitted no invalid-thread-handle report.

## What would falsify it

Any duplicated H_THREAD handle that resolves to a different GuestThread record, fails thread control or wait semantics while another alias remains open, or is not signalled when that thread completes.
