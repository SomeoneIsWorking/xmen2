---
id: C151
kind: claim
status: falsified
created: 2026-08-11
tags: threads,movie
falsified_on: 2026-08-14
---

## Claim

The movie player's threads and the main thread rendezvous correctly under one global lock; what looked like a threading-model limit was a stale handle

## Evidence

18 guest threads created, 18 exited, 18 reaped across six movies played in sequence, ~50 presents/s sustained, and the run continues past the movies into XMen2.exe. The threading model is unchanged -- one guest thread runs at a time -- so the caveat in threads.c's header (a decoder cannot keep up with a main thread that never yields) was NOT what this was. Three supporting mechanisms were needed and are verified by the same run: timers pumped from inside a blocking wait, the wait's timeout taken from the next timer's due time (1.3 fps -> 40 fps), and PulseEvent with a pulse generation for the manual-reset case.

## What would falsify it

a movie that stalls again, or any run where a guest thread is resumed by a handle that names a different thread than the guest meant. The per-thread suspend/resume counts in the exit report are the check: a live thread with zero resumes next to a dead one with millions is the signature.

## FALSIFIED 2026-08-14

A later smoke_loop stalled in the intro with libCriMovie's decoder suspended while ResumeThread(0x24) reported that the handle named no guest thread. This is exactly C151's stated falsifier. Static RE then found libIGCore FUN_10075400 calling GetCurrentThread and DuplicateHandle at 0x10075478; the host duplicate had no shared GuestThread identity.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
