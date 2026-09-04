---
id: C264
kind: claim
status: holds
created: 2026-08-25
tags: input,pad,dinput,hotswap,save,continue
depends: src/native/dinput8_controller_slots.c#dinput8_controller_slots_probe, src/native/dinput8_hotplug.c#dinput8_check_controller_table, src/native/input_probe.c#input_probe_report, tools/live_case.py#case_pad_after_load
---

## Claim

A controller attached AFTER a save load is polled by the game's own loop and acts

## Evidence

tools/live_case.py pad-after-load, 11/11 twice on scratch/build-native (2026-08-25). Boot Continue loads autosave.save; a synthetic pad attaches at frame 2000, after the payload deserialized. The poll-side probe then reports FUN_006285c0's OWN state: slot 0 device 0x71801f50, attached-table yes, last frame READ; slots 1-9 NULL and skipped; mask 0x00000001. The heartbeat grew 0 -> 3810 button reads, which is the game's per-frame loop and not the probe (the probe adds a handful). Start at frame 2463 changed the presented frame, mean |delta| 22.4. The decisive narrowing came from reading the loop instead of watching writes: FUN_006285c0 walks ten slots unconditionally from manager+0xc (0x6287e6 LEA EBX,[EAX+0xc]; 0x628870 CMP EAX,0xa) and skips a slot only on a NULL interface pointer (0x6287f0 TEST/JZ), setting 1<<slot in manager+0x129cc on a successful GetDeviceState(0x110) (0x628848). There is no deserialized count and no per-slot enable, so a slot the game never reads could only be a NULL slot -- and it is not NULL. This closes issue #117; its earlier negative is unexplained, with a stale binary recorded only as a hypothesis rather than a finding.

## What would falsify it

a pad-after-load run whose poll-side block shows the pad's slot NULL or not read, or whose heartbeat stays at the probe baseline; or a real (non-synthetic) controller behaving differently from the synthetic one on the same path
