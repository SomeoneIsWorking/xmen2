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
around `0x005f4ac4`) takes ONE whitespace-delimited token and hands it to the
script manager's `+0xc` slot, `FUN_004a11c0`:

- A token without `(` is a file name: `runscript
  act1/genosha/genosha1/temp_addmoney` reads
  `scripts/act1/genosha/genosha1/temp_addmoney.py` whole through the file
  system and splits it on `"\r\n"` (`0x0068d350`) only.
- A token with `(` is code, split on the literal four characters `\n\r`
  (`0x0068d348`): `runscript setPosX('_HERO1_',701)` runs. Code with a space
  is cut there by the one-token rule.

`FUN_004da450` passes each piece to the statement parser `FUN_004d9740`,
which appends every argument to one pooled 0x38-byte node: seven inline slots
at `node+8`, count at `node+0x24`, no bound check. An eighth argument lands on
the count, and freeing the node (`FUN_004d78a0`) then walks that pointer-sized
count off the heap. Every shipped script is CRLF with at most a few arguments
per line; a file with bare LF breaks is one statement holding every line's
arguments, which is how four two-argument `setPosX` lines crashed.

The port's `GET /script?text=CODE` writes CODE to
`<run dir>/overlay/scripts/x2/control/<n>.py` with every line break as CRLF
and queues `runscript x2/control/<n>`. The guest opens that file through the
run's generated-asset overlay (`k32_set_generated_assets`, after `X2_ASSETS`).
The route drops the `host_dir_cache` listings for the directories and file it
creates; without that, every script after the first resolved against the
listing taken at the first open and was not found.
`tools/live_case.py script-lines` runs a 4- and a 12-statement script in one
run and checks both took effect.

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
  The lookup is case-sensitive and keeps the spawner's own spelling:
  Dead Zone 3's `Grizzly_spawner` (`instantspawn="false"`) spawns
  Grizzly as `_ms_Grizzly_spawner`; `_ms_grizzly_spawner` returns NULL.
- The new entity's handle is at `entity+0x1c`, the handle `/entities` reports.

`src/native/entity_spawn_probe.cpp` logs every spawn with the factory's
entity and handle (`spawn: BehavEd spawn #N, entity 0x... handle H`).

Measured: Continue, `loadmap act1/deadzone/deadzone2`, `/script` with
`spawn('_ACTIVE_HERO_','_ms_ss_mikhailranger_a','x2_spawn',' 120 0 0 ',' 0 0 0 ')`
created handle 1787 at (2860, 1460, 2) beside the party, and the trace named
the same handle (`tools/live_case.py ranger-render`).
