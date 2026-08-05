---
id: 13
title: Native run: a guest RET pops a stack address deep in the exe's startup
status: open
symptom: x86_return_to: 0x300ffef4 is not a function entry -- the popped value is an address in the GUEST STACK, not code. Happens after all seven DLLs initialise and the exe's CRT startup completes
tags: pc,recomp,native,esp,translation,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Where it stops

Everything before this works: eight modules map with relocations applied, 2841
imports bind, all seven DLLs run DllMain and return TRUE, the exe's CRT startup
completes, and execution is well into the exe's own code.

Then a RET pops 0x300ffef4 -- an address inside the guest stack
(0x30000000..0x30100000), not a code address.

## What the boundary ring shows

src/native/x86rt_native.c now records every guest/host crossing with ESP on
both sides. The window before the failure is:

    guest 0x005d7940  esp 300ffe4c -> 300ffe54  (+8)  x2
    guest 0x005d7940  esp 300ffe60 -> 300ffe68  (+8)  x2
    guest 0x00597b30  esp 300ffe60 -> 300ffe68  (+8)
    guest 0x005d7940  esp 300ffe7c -> 300ffe84  (+8)  x8
    guest 0x00597b60  esp 300ffe7c -> 300ffe84  (+8)
    ...

ESP climbs steadily toward the top of the stack. Two things follow:

* Every crossing in the window is guest-to-guest. NO import crossing appears,
  so this is not a stdcall/cdecl arithmetic error in the hand-written Win32 or
  CRT layer -- that was the first hypothesis and the ring rules it out.
* The functions involved are small helpers. 0x005d7940 is four instructions
  () and balances
  correctly on its own: +8 for a , which is right.

## What that leaves

A translation-level imbalance somewhere upstream that the ring's window does
not yet reach, or a function whose detected boundaries are wrong so its
epilogue does not match its prologue. The second is plausible given how much
function detection has been corrected in this module already.

## What to try next

* Widen the ring, or record only crossings whose ESP delta is NOT the expected
  4+4N -- the interesting ones -- so the window covers more history.
* Check whether 0x00597b30 / 0x00597b60 have plausible boundaries: they are
  adjacent, 0x30 apart, which is the size of a small thunk and also what a
  mis-split function looks like.
* The x86_return_to path itself is worth auditing: it re-dispatches on a RET
  whose popped value differs from the entry return address, which is a tail-call
  emulation. If it fires spuriously it would consume return addresses and make
  ESP climb exactly like this.
