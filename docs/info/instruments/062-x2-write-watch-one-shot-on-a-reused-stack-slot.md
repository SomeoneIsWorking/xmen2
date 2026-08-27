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
- Native CRT `memcpy`, `memmove`, `memset`, `strncpy`, and `strncat` publish a
  watched dword after any covering bulk write. Previously only generated
  `WR8`/`WR16`/`WR32` stores fired the watch, so a matrix copied as a 64-byte
  block reported its zero-initialization and silently missed its real value.
- Generated `WR64`, `WRF32`, and `WRF64` stores now publish either covered
  dword. The selector investigation exposed this second blind spot: an x87
  `FSTP` wrote the matrix scale through `WRF32`, while the watch reported only
  earlier integer zero-initialization and falsely implied that no generated
  body wrote the nonzero value.

## What it still cannot see

Non-CRT native writes that cover the slot without starting at its exact
address. Generated 64-bit stores and the native CRT bulk-copy boundary both
cover their complete written ranges.
