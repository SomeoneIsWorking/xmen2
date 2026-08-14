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

## Status

IN RE. The prerequisite is complete: the DirectInput path enumerates, opens,
configures and reads an SDL3 pad, including late attach/detach. A fresh registry
run with `X2_VIRTUAL_PAD=1` measured the next boundary: the game persists the
pad GUID slots but leaves every action's pad-binding slot empty, so it does not
auto-populate defaults merely because a controller exists.

The Xbox controller-options FB is now parsed by `tools/extract_fb.py` (C184):
eight members, including the controller diagram and the localized menu data.
That gives the authored controller screen but is not yet the action table. The
next RE step is to trace the Xbox code that supplies those assignments and
relate its action identifiers to the PC action records; the diagram alone is
not sufficient evidence for a hand-typed mapping.
