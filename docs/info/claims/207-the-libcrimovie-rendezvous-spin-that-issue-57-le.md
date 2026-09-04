---
id: C207
kind: claim
status: holds
created: 2026-08-16
tags: threads,movie,override,spin
---

## Claim

The libCriMovie rendezvous spin is replaceable by a bounded wait because the
decoder parks itself when armed: the measured control made 170.9M crossings
and 9,000,454 resumes versus 47.0M crossings and 448 resumes with the native
override.

## Evidence

FUN_10002520 is a zero-argument callback reached through the callback table at
FUN_100084d0/FUN_10008500. It sets flag 0x100572b0 to 1, sets the decoder's
spin priority at 0x10057294, then loops over `ResumeThread` and
`SetThreadPriority`. The decoder at FUN_10002630 parks when it has no work or
when the flag is set; its park path clears the flag and calls
`SuspendThread(self)`. The loop therefore polls for a park that it also arms.

The module-qualified native override in `src/native/movie.c` performs the same
flag arm, one resume, and priority update, waits through
`guest_cond_wait_ms` with a 1,000-wait bound, restores priority, preserves the
retail return and stack contracts, and can call the original body through
`x86_guest_body` for A/B. In matched 3,400-frame observations, the control made
170,937,598 crossings and 9,000,454 resumes across 240 suspends; the native path
made 47,035,669 crossings and 448 resumes across 236 suspends. Both reached the
frame cap cleanly.

## What would falsify it

a run where the decoder's per-park resume count stays in the thousands (thousands of parks) while the override announces a successful wait, or a fix run that reaches X2_MAX_FRAMES=3400 but shows live-lock-level crossings identical to control
