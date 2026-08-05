---
id: 13
title: Native run: a guest RET pops a stack address deep in the exe's startup
status: resolved
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

### Resolution (2026-08-06)
ROOT CAUSE FOUND: FUN_00554ba0 in XMen2.exe is TRUNCATED -- its last instruction is 'MOV ECX,ESI', not a RET.

The emitted C therefore falls off the end of the body and returns with whatever
ESP the partial body left (two un-popped pushes and an un-torn-down SEH frame).
Its caller FUN_0067c850 -- a static constructor -- then does 'pop ecx; ret' on a
shifted stack, and the RET pops a saved frame pointer instead of its return
address. That is the 0x300ffef4 the run died on.

The function is clamped just before the next detected start at 0x00555024, so
the 'next function' sits inside its real body. Same class as issue #8 / C077 on
the Xbox side.

## How it was found

Not by reading code. Three instruments in sequence, each answering one question
the previous could not:

  1. A boundary ring with ESP on both sides ruled OUT the hand-written Win32
     and CRT layer -- every crossing before the failure was guest-to-guest.
  2. x86_return_to gained a fire counter, which killed the leading hypothesis:
     it fired exactly ONCE, so it was not looping and consuming return
     addresses.
  3. The emitted epilogue now passes its OWN entry point and expected return
     address to x86_return_to, which named FUN_0067c850 directly; from there
     its two callees were three minutes of reading.

## Scope, measured

tools/verify_export.py now counts truncated bodies:

    XMen2       42 of 14929 (0.28%)
    libIGGfx     4 of 4742  (0.08%)
    libIGCore    1 of 5966  (0.02%)
    everything else 0

53 functions across the project. Small, bounded, and now visible on every run
of the checker rather than discovered by a crash.

## Fix, not yet done

Each truncated function needs the spurious function inside it removed and the
outer one re-created over its full body -- the inverse of SplitFunction.py.
VERIFY_TRUNC_OUT=<file> writes the list.
