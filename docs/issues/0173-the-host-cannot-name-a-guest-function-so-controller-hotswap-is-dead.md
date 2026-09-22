---
id: 173
title: the host cannot name a guest function, so controller hotswap is dead
status: resolved
symptom: XMen2.exe's export directory is RVA 0 size 0, so x86_native_entry_containing cannot name the game's re-enumeration routine
state_items: S020,S006
tags: input,pad,hotswap,pe,symbols
created: 2026-09-19
updated: 2026-09-22
---

# 0173 — the host cannot name a guest function, so controller hotswap is dead

State items: S020 (platform-neutral touch play), S006 (input and controllers)
Status: resolved by option 2 below (see Resolution).

## Symptom

`tools/live_case.py pad-late` fails two checks on the current revision:

```
[PASS] synthetic pad attached late
[FAIL] hotswap re-entered the game's enumeration
[PASS] Start press delivered to the synthetic pad
[FAIL] the presented frame changed after Start (mean |delta| 2.0 > 8)
```

The run says why:

```
DINPUT8: a pad appeared, and this host never identified the game's
         enumeration routine, so generation 1 cannot be synchronized.
```

## Cause

`dinput8_hotplug_note_game_enumeration` identifies the game's re-enumeration
routine from the address `EnumDevices(GAMECTRL)` returns to, through
`x86_native_entry_containing`, which resolves **only PE exports**. XMen2.exe's
export directory is empty — RVA 0, size 0 — so no address inside it can ever
be named.

The diagnostic now says this at the point it happens rather than minutes later
at the pump:

```
DINPUT8: EnumDevices(GAMECTRL) returns to 0x00628e57, in XMen2.exe, and no
         exported entry sits at or below it -- so the game's own
         re-enumeration routine cannot be named, and a pad that arrives later
         cannot be admitted by the game's rules.
```

0x00628e57 is inside FUN_00628e20, exactly where claim C161 says the call is,
so the guest boundary is intact. What is missing is a name.

## Why it used to work

C161's evidence (2026-08-12) records "the routine found at runtime
(0x00628e20 FUN_00628e20)". A `FUN_`-prefixed name is not a PE export; it came
from the guest corpus's own symbol table, which commit 89de118 retired along with
the generated-source corpus itself.
`x86_native_entry_containing` kept its signature and now silently answers 0.
C161 and C262 are marked falsified.

## Worked around, for touch only

`x2::input::touch_pad::prepare_for_host()` attaches the overlay's pad when the
window arrives, which is about two seconds before the guest enumerates, so
touch needs no admission at all. `tools/live_case.py touch-pad` passes 7/7
with 27,700 pad button reads, 43 of them DOWN, and a Start press that moves
the presented frame by 9.2. That does nothing for a real controller plugged in
mid-game.

## The fix this needs

A durable way to name a guest function in an image that exports nothing. The
options, in the order they should be considered:

1. Commit recovered function boundaries for XMen2.exe as redistributable
   analysis metadata and resolve against that, restoring the original design
   (identify the caller, do not hardcode it).
2. Declare the routine's entry point as recovered metadata beside the other 94
   native overrides, and verify it at runtime against the observed return
   address and the resulting admission, refusing on a mismatch.

Whichever is chosen, the negative must stay loud: acting on a wrong answer
means calling an arbitrary guest routine with a fabricated argument, which is
what C161's own falsifier warns about.

## Resolution

Option 2. `dinput8_hotplug.c` declares the routine (XMen2.exe FUN_00628e20)
and its EnumDevices(GAMECTRL) call site (0x00628e57) as recovered metadata,
maps both through the module's actual base, and admits the entry only when the
game's own enumeration is observed returning to that site. A GAMECTRL
enumeration from anywhere else is refused with its address and never admitted.
The identity of the image itself is already pinned by `GAME_MODULE_SHA256`.

`tools/live_case.py pad-late` passes 8/8 (it failed the HOTSWAP and Start
checks above), and `pad-persisted` 9/9: a pad attached 600 frames after start
whose stored id names Player 1 is admitted by the game (guest slot -1 -> 0)
and its Start press moves the frame. A late pad with NO stored or session
assignment is still not bound to a player; that is the assignment policy, not
admission. Claims C161 and C262 hold again.
