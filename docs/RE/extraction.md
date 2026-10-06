# Extraction points and hero revival

Addresses are linked VAs (image base 0x400000, identity-mapped at run time).

## The extraction point

There is no extraction entity class with a use handler. A pad is an ordinary
entity whose scripts live in `Scripts/common/extraction/`:

- `exp_spawn.py` (spawn script): if `isExtractionPointUnlocked()` the pad acts at
  once, otherwise it spawns the `expt_trig` proximity trigger and parents it.
- `exp_trig.py`: the trigger acts on its parent (first touch unlocks the pad).
- `exp_activate.py`: `_ACTIVATOR_ != _OWNER_` (a hero uses the pad) calls
  `extractionPoint("_OWNER_")`; otherwise `extractionUnlock("")`.

BehavEd commands (table at 0x0068b3d0, `{handler, name, ret, args}`):

| command | handler |
|---|---|
| `extractionPoint(a)` | `0x004a6b50` |
| `extractionPointLite(asss)` | `0x004a6d80` |
| `extractionPointChange(ai)` | `0x004a7020` |
| `extractionUnlock` | `0x0049f1e0` |
| `isExtractionPointUnlocked` | `0x0049f1a0` |

`0x004a6b50` locks controls, then queues console commands `openmenu('worldmap')`,
`extractionPointChange(%d,0)` and `saveloadProcess(4)`. It never touches hero
health or the dead state. `CExtractionPointSystem` (vtable 0x00686dac) only
stores per-point unlocked bits (serialised by 0x00468380/0x004683e0).
`loadextraction %s recall|extract` (console, `0x0046e160`) is the travel half.

## Revival in retail costs money

A fallen hero stays down; nothing at the pad revives it (observed live on
`act1/genosha/genosha4`: killed Wolverine with `runscript killEntity("wolverine")`,
no recovery in 20+ s, none from the Use key beside the body).

Revival is a `CMenuTeam` action (vtable 0x006a2c94). The cost:

- `0x004b8830`: `max(200, 2 * level * level)`, level from `0x004b87c0`.
- Funds: the money singleton `0x00480a00` (object `[0x0072a514]`), getter
  vslot `+0x24` = `0x0047b330` reads `obj+0x2590`; setter vslot `+0x6c` =
  `0x0047b400` writes it.
- Display: `0x005bdf50` (char summary) prints `Revive: <cost>` (string 1023),
  red when cost > money; `0x005deb60` shows the `$MENU_ACCEPT Revive` hint
  (string 1002) for a dead hero.
- Action: roster accept handlers `0x005e4010` and `0x005e5280`. Preconditions:
  stats vslot `+0x28` (dead) true, `(DAT_008b134c & 0x10) == 0`, and
  `money >= cost` (`0x005e5359`). Effect: money -= cost, stats vslot `+0x1c`
  (revive the stats object), then for each party actor whose `actor[0xd7]` is
  that stats object: vslot `+0x1a4` (health) and `+0x21c(0, DAT_0069d058, 0.5, 1, 0)`.
- Roster mode is `DAT_008b134c & 4`, set by `0x005f4770` (`loadmap <name> <n> 1`
  choose-team load) and `0x005e35d0`; in it a dead party hero is moved to the
  roster.

Live proof (scratch run, money forced to 500, flag 4 forced): the roster showed
`Wolverine  Revive: 200` with an empty health bar.

## The revive function

`CActor` vtable `0x00682ccc` slot `+0x21c` = `0x0041dd80`
`(delay, id, healthFrac, flag, arg)`: clears the dead bit, queues a timer
(`this+0x340`), stores `healthFrac`. The timer end runs `0x004220d0`, which sets
health to `max * healthFrac` and plays `resurrect_v` or `getuponfront`.
Other callers: `resurrect(a)` script command (`0x004a3b70`, 0.5), the
`CCEResurrect` combat event (`0x004e9fa0`, power based, ally revive via
`0x00428ce0` within 480 units), and AI.

## Not established

- The dead flag's exact field: the stats object's vslot `+0x28` predicate
  (`actor[0xc0]`), not decompiled.
- How the world-map choice leads into the roster team menu after an extraction.
- Whether `DAT_008b134c & 0x10` is multiplayer or something else.
