---
id: C207
kind: claim
status: holds
created: 2026-08-16
tags: threads,movie,override,spin
---

## Claim

The libCriMovie rendezvous spin that issue #57 left running is collapsible into a bounded wait because the decoder parks itself when armed: control spins 170.9M crossings vs fix 47.0M (3.6x fewer), decoder resumes 9,000,454 -> 448 per run

## Evidence

FUN_10002520 (the partner: the spin's 3M-iteration ResumeThread/SetThreadPriority loop, reached only via the callback table at FUN_100084d0/FUN_10008500) is a 0-arg callback whose every effect on the decoder is an ARMED park: it sets flag [0x100572b0]=1, sets the decoder's spin priority [0x10057294], then SetThreadPriority+RResumeThread loop. The decoder (FUN_10002630, emitted libCriMovie_000.c:8220) parks when it has no work OR when flag==1 (CMP [0x100572b0],EDI; JNZ skip, park block clears flag to 0 and SuspendThread(self)). So the spin is a poll for the park: armed flag => decoder suspends itself momentarily and must be resumed. The override __wrap_fn_libCriMovie_10002520 (src/native/overrides.c) reproduces exactly the flag-arm + one resume + set spin priority, then WAITS bounded (guest_cond_wait_ms 1ms, MAX 1000 waits) instead of spinning, then restores priority and returns EAX=SetThreadPriority's return (1), keeping esp+=4. A/B in the SAME binary via X2_SPIN env (spin=defer to real body; unset/0=wait). Same scripted run, X2_MAX_FRAMES=3400 both: control 170,937,598 total boundary crossings vs fix 47,035,669; decoder tid 1021 control '240 suspend(s) 9,000,454 resume(s)' vs fix '236 suspend(s) 448 resume(s)' (448/~1.9 per park, ~37500 resumes/park before); fix load-window imports ResumeThread 928,572 cumulative-max and SetThreadPriority dropped out of the top-import list after control showed 6,000,196/6,000,093; 0 DEFERRING logged; both runs reached frame 3400 cleanly. Stack effect fidelity enforced by keeping the recompiled body alive (--isolate into libCriMovie_000.c) and -Wl,--wrap: the real body stays linked and diffable, --wrap redirects the cross-object dispatch-table data reference in libCriMovie_native.c.

## What would falsify it

a run where the decoder's per-park resume count stays in the thousands (thousands of parks) while the override announces a successful wait, or a fix run that reaches X2_MAX_FRAMES=3400 but shows live-lock-level crossings identical to control
