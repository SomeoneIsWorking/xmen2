---
id: C161
kind: claim
status: holds
created: 2026-08-12
tags: input,controller,hotswap
---

## Claim

Controller hotswap works through the game's OWN re-enumeration routine (FUN_00628e20), not around it

## Evidence

X2_VIRTUAL_PAD=f1500 attaches a pad 1500 frames after the game enumerated; the run reports the routine found at runtime (0x00628e20 FUN_00628e20) and ends with 'gamepad acquired, 2387 state read(s), 2388 Poll(s), 1 Acquire(s)'. With f1200-2200 the state reads stop at the unplug while polls continue, so a disconnected pad reports as disconnected. The direct-callback approach was tried first and MEASURED to fail (created and configured, 0 reads) because FUN_00628b40 only stores a device whose GUID is in the table at this+0x27e8, which it writes only while this+2 is set -- and this+2 is FUN_00628e20's own argument.

## What would falsify it

A hotswap run where the report shows the pad acquired but with a state-read count far below the frames since it arrived, which would mean the game admitted it and is not polling it. Or x86_native_entry_containing naming a function other than FUN_00628e20 at the EnumDevices call site -- the host prints what it found for exactly this reason, and acting on a wrong answer would call an arbitrary routine with a fabricated argument.
