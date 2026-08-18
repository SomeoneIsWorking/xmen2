---
id: I062
kind: instrument
status: fixed
created: 2026-08-18
tags: watch,stack,diagnostic,lied
---

## Instrument

`X2_WRITE_WATCH=<guest-addr>` -- reports the running body when a guest address
is written (`WR8`/`WR16`/`WR32` in `src/recomp/x86rt.h`, fired through
`x2_write_watch_fire` in `src/native/x86rt_native.c`).

## How it lied

It was ONE-SHOT: the first write whose value differed from the process cookie
disarmed it. On a GUEST STACK address that is wrong by construction -- a stack
slot is reused by every frame that passes through it, so the first write is
almost never the interesting one.

Armed on `FUN_0046b750`'s /GS cookie slot to catch a supposed overrun, the
single shot was spent on `FUN_00679e40` writing `0x00415108` there, hundreds of
frames before the event. The watch then sat disarmed through the entire window
it existed to observe and reported nothing further. Issue #81 records the
resulting red herring as if it were a finding.

The failure is silent in the worst way: the watch DID print something, so it
read as a working instrument that had answered.

## Fixed

- No longer one-shot. Every matching write is reported.
- Optional `:<value>` filter (`X2_WRITE_WATCH=0x700ffedc:0`) -- the way to ask
  "who writes ZERO here" on a hot slot instead of "who got here first".
- Unfiltered, it caps the BORING case (first 8 writes) but ALWAYS reports the
  two state changes that matter on a /GS slot: a store of the process cookie
  and a store of zero.
- `x86_write_watch_hits()` exposes the denominator, so a report can say
  "0 of 12,043" rather than printing nothing.

## What it still cannot see

Writes wider than 32 bits, and writes that land on the slot without starting at
its exact address -- the compare is address equality, not range overlap. A
memcpy stepping over the slot from below is only caught if one of its stores
begins exactly there.
