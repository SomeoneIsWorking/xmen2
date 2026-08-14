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

## Implemented verified subset

`src/native/xbox_defaults.c` joins that executable evidence to the authored
Xbox controller screen. C187 records the result and its falsifier. It installs
17 assignments:

- left stick movement and right stick camera;
- A Punch, B Slam, X Use/Pickup/Boost, Y Jump/Xtreme;
- LT Call Allies and RT Mutant Powers;
- Back Team Information, Start Pause, and right-stick click Map Toggle.

The hook is deliberately after the real `FUN_0061b030`, so ordinary settings
load first. It writes through retained `FUN_006297a0`, not direct table stores.
If any pad binding already exists, the entire automatic preset defers rather
than partially merging around user state. Repeated hotswap pumps are
idempotent. When the last pad disappears it clears only tuples that still
match what the port installed; a user-modified tuple is not agent-owned cleanup.

`tests/test_xbox_defaults.c` calls the shipping wrapper and checks the retained
body, all 17 exact tuples, the repeat gate, disconnect removal, and custom-map
refusal. The combined native suite passes 45/45. A real 1,800-frame run with
the virtual Xbox pad reports one preset install through the retained setter.

## Remaining evidence boundary

The preset is not yet the complete Xbox release mapping:

- The Xbox screen says only **Change Hero** for the d-pad. The XBE contains
  `NEXT`, `PREV`, `INC_AGGR`, and `DEC_AGGR`, but that does not prove which
  physical direction drives which action—or that all four are the d-pad.
- Black/White are authored as **Use Health Pack** / **Use Energy Pack**, while
  the PC's 42 named rows expose neither action. Aliasing them to a `QuickPower`
  row would be a guess.
- The mapping menu still needs two explicit commands: Keyboard Defaults and
  Xbox Defaults. The engine-side preset is now reusable by the latter, but the
  UI command path has not been ported.

Those omissions are visible in the runtime install message. They are not
silently replaced with plausible controls.
