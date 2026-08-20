# Controller mapping: a "defaults" option, and what XBOX DEFAULTS means

Feature 2 of the three (`README.md`) is auto controller mapping via SDL's
gamecontrollerdb. On top of that, the mapping UI gets a **defaults** option, and
the default it offers is not invented here:

> **Xbox defaults** = the mapping the game's own XBOX PORT shipped with.

That is a fact to be READ OUT OF THE XBOX BUILD, not designed. The Xbox release
is in this repository's sources already (`$XBOX_ISO`, `xbox/`), it is the same
game by the same developers, and its button assignments are the ones a player
who knows this game on a console expects. Anything we invent instead is a guess
competing with a shipped answer.

## What has to be found, before any of it is written

- **Where the Xbox port stores its default pad mapping.** `default.xbe` is
  lifted by the same recompiler (`xbox/`, `tools/xbox_relift.sh`), so the table
  can be located the same way every other structure in this project has been:
  find the code that reads it, not a plausible-looking blob.
- **The ACTION set it maps to.** The PC build's `x2_button` enum
  (`src/display/ig_controller.h`) is the engine's controller button numbering,
  not the game's actions. The Xbox table maps ITS buttons to game actions, so
  the two have to be related through the action names, not through button
  indices that happen to line up.
- **Whether the Xbox build has more than one** (a menu set and a gameplay set,
  or per-character sets). "The default mapping" being singular is an assumption
  until the read says so.

## Rules this inherits

- It is an RE step, so it goes on the frontier and is `⛔ hack` until the table
  is read from the real build and matches on real data. A hand-typed mapping
  that "looks like an Xbox pad" is exactly the faked step
  `docs/../re_frontier.py` exists to prevent.
- No Xbox asset or table is committed. It is read from `$XBOX_ISO` at build or
  run time like every other piece of game content here.

## Recovered PC binding engine

The PC side is no longer inferred. `FUN_0061b030` names all 42 rows and loads
their two persistent keyboard/mouse slots. `FUN_006294b0` reads a row; slot 2
is tried first by the prompt path. `FUN_006297a0(row, slot, kind, code)` is the
exact setter. Axis names in `FUN_006281f0` establish the signed codes:

```
LX+ 1  LX- 2  LY+ 3  LY- 4
Rx+ 7  Rx- 8  Ry+ 9  Ry- 10
POV X+/X-/Y+/Y-  0x11..0x14
A/B/X/Y           0x15..0x18
Back/Start/LS/RS  0x1b..0x1e
LT/RT on combined Z+/-  5/6
```

`FUN_00619c40` is the other half: its action switch maps the common console
action identifiers to PC rows. The alignments are distinctive, not positional
guessing: POWER 7 -> row 8 `Power`, GUARD 8 -> row 7 `Guard`, ALLY 9 -> row 9,
NEXT/PREV 11/12 -> `NextHero`/`PreviousHero`, MAP_TOGGLE 15 -> row 16, and
INC/DEC_AGGR 16/17 -> the correspondingly named PC rows.

## Implemented bindable layout

`src/native/xbox_defaults.c` joins that executable evidence to the authored
Xbox controller screen. C187 records the result and its falsifier. It installs
21 assignments:

- left stick movement and right stick camera;
- A Punch, B Slam, X Use/Pickup/Boost, Y Jump/Xtreme;
- LT Call Allies and RT Mutant Powers;
- d-pad Up/Down/Right/Left as Next/Previous/Increase/Decrease hero;
- Back Team Information, Start Pause, and right-stick click Map Toggle.

The d-pad order is executable evidence, not a reading of the diagram:
`default.xbe` `sub_00162240` registers `DPAD_UP` with action 11 `NEXT`,
`DPAD_DN` with 12 `PREV`, `DPAD_RT` with 16 `INC_AGGR`, and `DPAD_LF` with 17
`DEC_AGGR`. The PC action switch maps those IDs to rows 12, 13, 15, and 14;
the PC physical-name function maps POV Up/Down/Right/Left to codes
`0x14/0x13/0x11/0x12`.

`src/native/xbox_defaults.c` owns only the 21 evidence-derived tuples.
`src/input/player_input.c` is the single publisher: it resolves each player's
persistent device assignment and writes the fixed table to slot 1 of the
master, working, and menu sets through `input_bindings_write_player`. The pure
test assigns pads to different players and proves the running sets receive the
same canonical codes. `tests/test_xbox_defaults.c` separately pins every tuple
and rejects duplicate action rows.

## RmlUi player assignment

The shipped RmlUi overlay now owns settings. Each player selects None, Auto,
Keyboard, or one persistent connected pad. A controller page is intentionally
read-only about actions: it states that the canonical Xbox/PS2 layout is used.
Keyboard mappings live in four reusable profiles instead. This retires the
old adapter around the PC executable's `Defaults 1/2/3` buttons; keeping it
would give the guest editor and RmlUi two competing writers for the same slots.

## Remaining evidence boundary

The preset is not yet the complete Xbox release behavior:

- Black/White are authored as **Use Health Pack** / **Use Energy Pack**, while
  the PC's 42 named rows expose neither action. Aliasing them to a `QuickPower`
  row would be a guess. The Xbox common-action constructor does not register
  Black or White either, proving pack use is a separate direct gameplay path.
  The next layer is partly recovered. Xbox `sub_00163240` registers BLACK with
  physical-value source index 8 and WHITE with source index 9 (their separate
  platform codes are 9 and 8). `sub_00163E40` expands the Xbox analog-button
  bytes into a 30-float array, and `sub_0015FD90` copies that array into each
  per-player controller separately from the digital-button mask. Per-player
  vtable slot `+0x10` is now **resolved, not inferred** (C189): the controller
  class is the one with vtable `0x004A9D6C`, and that slot holds
  `sub_0015F5B0`, which returns the float at `[this + index*4 + 0x2fc]`. Its
  setter is slot `+0x38` (`sub_0015F5C0`), and the array is exactly 30 entries
  wide -- it ends at `+0x373`, abutting the previous-digital-mask word at
  `+0x374` -- matching the 30 binding records of `0x14` bytes the constructor
  `sub_0015F460` builds at `+4`. Black/White therefore do **not** need to
  become action-mask bits; the remaining trigger must read physical indices
  8/9 directly or through a wrapper. The earlier
  `0xA` logical-action query is party switching, and the action-8 query in
  `sub_001DCA40` is menu navigation, not pack use. Xbox `sub_00088680` is the
  counterpart of PC `FUN_0047a140`; both retain separate `HEALTH_ITEM` and
  `ENERGY_ITEM` consumption branches. The PC therefore already owns the
  consumption logic. What remains is the Xbox-only physical trigger into that
  retained event path, not a native reimplementation of inventory or healing.

  Two routes to that trigger are now closed, and closed with their
  denominators rather than by giving up:

  - **No literal index.** Across 22,931 disassembled functions there are 884
    `call dword ptr [reg + 0x10]` sites; 323 push a literal last and only 6
    push 8 or 9 -- all 6 through `sub_0005B200`, whose `+0x10` is the
    15-bit flag setter `sub_0005AD30`, not a float read. The scan's blind spot
    is large and stated: 415 sites push a register and 146 push nothing in the
    window, so it cannot see 561 of the 884 (C190). What this rules out is the
    literal-index route, not the trigger.
  - **No searchable name.** `HEALTH_ITEM` and `ENERGY_ITEM` are item-TYPE
    names, entries 0 and 1 of the pointer table at `0x0053FEBC` (then
    `XTREME_PIP`, `SKIRMISH_KING_PIP`, `KEY1..3`, `KEYCARD1..3`). Neither
    string is referenced by any instruction, so no string-reference walk
    reaches the pack path (C191).

  The indirect route is now resolved end to end (C192). Gameplay reaches a
  controller through the input manager, never by indexing the array itself:
  all nine instructions in the image that multiply by the `0x388` element
  stride (`0x0015F95C`..`0x001603C0`) are inside the input module.

  ```
  sub_00160E60()            -> input manager   (object 0x0061d060, vtable 0x004A9DAC)
      manager slot +0x4c    -> sub_0015F940(player) -> controller  (vtable 0x004A9D6C)
          controller +0x10  -> sub_0015F5B0(index)  -> physical float
  ```

  Following the register chain from the 356 `getController` sites attributes
  246 virtual calls to the returned controller. **Every literal physical index
  ever read through `+0x10` is one of 0, 1, 2, 0xb, 0xc, 0x10, 0x11 — never 8
  or 9.** The only two non-literal readers are in `sub_001E4210`, a virtual
  method (slot `0x2c` of the vtable at `0x004B2600`) that forwards its own
  first parameter as the index; its callers are the next thing to enumerate.
  The model is corroborated from the other end: manager slot `+0x24` is
  `sub_0015FD10`, and the analog-byte expander `sub_00163E40` calls exactly
  that slot — the same function that copies the 30 floats into the controller.

  Reproduce all of it with `python3 tools/xbe_query.py chain 0x4c --slot 0x10`.

  `tools/xbe_query.py` is the tooling all of this was measured with; it
  replaces the ad-hoc scratch scripts each of these questions used to need.
- Real controller identity and assignment still need a hardware capture. The
  virtual-pad and pure tests prove the model and publication path, but this
  machine has no physical controller attached.

Those omissions are visible in the runtime install message. They are not
silently replaced with plausible controls.
