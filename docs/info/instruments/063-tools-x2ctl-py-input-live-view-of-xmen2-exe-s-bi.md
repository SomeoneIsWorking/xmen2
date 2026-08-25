---
id: I063
kind: instrument
status: trusted
created: 2026-08-18
---

## Instrument

tools/x2ctl.py input -- live view of XMen2.exe's binding table, the game's own DIJOYSTATE2 block, and the pad manager's per-player physical floats and logical mask

## Validated by

Run against both classes in the same session: idle showed 0 of 30 physical floats non-zero and no buttons down; Return held showed physical[4]=1.000; pad A held showed the game's DIJOYSTATE2 button 0 down with FUN_00627650(pad 0, code 0x15) returning 1.0. It also refused by name twice during its own bring-up (wrong controller-array address, wrong wrapper address) rather than printing an empty table.

## Known failure modes

- 2026-08-25, FIXED (issue #116): the probe EXECUTES guest code, so it was
  never purely passive -- its authored-conversation slot lookup ran the
  retail table walk at arbitrary moments, and a poll during map construction
  stricmp'd NULL names and crashed the whole run. The lookup is now gated on
  a visible conversation (the same production ordering). Any future probe
  addition that guest-calls retail must gate on the same state the retail
  caller does.
