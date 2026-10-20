# Console, runscript and spawn

The retail console (`FUN_0055c890()` = `&DAT_007ac290`, vtable `0x0069a81c`;
see [Boot](boot.md) for the vtable slots) is how the front end, scripts and
the port's control channel run commands. This doc owns what a command can do
once queued; `src/native/control_console.cpp` and
`src/native/control_script_route.cpp` are the port's callers.

## Queue and executor

- `FUN_0055c410` (console +0x1c) hands a command line to `FUN_0055c100`, which
  copies it into a 128-byte slot: 127 characters plus the terminator. The port
  refuses a longer command (`retail::FrontEnd::kCommandMax`) instead of letting
  it be cut.
- `FUN_0055beb0` (console +0x18) looks the first word up and runs it. An
  unknown name returns 0 and prints nothing; retail gives no feedback.

## runscript

The `runscript` handler `0x005f2350` (registered with name string `0x006a38e0`
around `0x005f4ac4`) takes ONE whitespace-delimited token and runs it as a
script file through the script manager `FUN_004a1670`: `runscript
act1/genosha/genosha1/temp_addmoney` opens
`scripts/act1/genosha/genosha1/temp_addmoney.py`. Inline code
(`runscript spawn(...)`) is read as a file name and never executes.

The port's `GET /script?text=CODE` writes CODE to
`<run dir>/overlay/scripts/x2/control/<n>.py` and queues
`runscript x2/control/<n>`. The guest opens that file through the run's
generated-asset overlay (`k32_set_generated_assets`, after `X2_ASSETS`).

## spawn

The script function `spawn(target, definition, name, position, orientation)`
is `FUN_004a4420`.

- `position` and `orientation` are strings of three numbers added to the
  TARGET entity's position and orientation, not world coordinates.
  `'_ACTIVE_HERO_'` targets the active hero.
- `definition` is an entity definition name, not an npcstat name. The factory
  `FUN_004612e0` looks it up through `FUN_004625c0()->+0x20` and returns NULL
  for an unknown name, or for the `actor` class when its budget check (< 10)
  fails. A map's `monsterspawnerent ss_x` places definitions named `_ms_ss_x`:
  `_ms_ss_mikhailranger_a` (npcstat `MikhailRanger_a`, Dead Zone 2),
  `_ms_ss_critter_a` (Dead Zone 1). `MikhailRanger_a` returns NULL.
- The new entity's handle is at `entity+0x1c`, the handle `/entities` reports.

`src/native/entity_spawn_probe.cpp` logs every spawn with the factory's
entity and handle (`spawn: BehavEd spawn #N, entity 0x... handle H`).

Measured: Continue, `loadmap act1/deadzone/deadzone2`, `/script` with
`spawn('_ACTIVE_HERO_','_ms_ss_mikhailranger_a','x2_spawn',' 120 0 0 ',' 0 0 0 ')`
created handle 1787 at (2860, 1460, 2) beside the party, and the trace named
the same handle (`tools/live_case.py ranger-render`).
