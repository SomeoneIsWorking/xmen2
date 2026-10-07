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

The handler's control lock is the party manager's flag byte `+0x728` bit
`0x20` (setter vtable `+0x7c` = `0x00469680`, getter `+0x80`), set by
`0x004a6b50` and `0x004a6d80`. It is not what holds the world map: with the map
up through the pad the byte reads `0x07` (bit `0x20` clear), the menu and edge
masks read as with a console-opened map, and after Back the party walks again.
The release is `FUN_005eb930` (menu stack empties; clears the bit through
`+0x7c(0)`). Back on the map failed for a menu-input reason, not a lock: see
[menus](menus.md), "Escape in a menu with a focused item".

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

## The port's revive option (`gameplay.extraction_revive`)

`off` (default) is retail. `free` and `paid` are port additions, owned by
`src/native/extraction_revive.cpp` (guest bridge), `extraction_revive_policy.cpp`
(cost rule), `src/input/revive_prompt.cpp` (the request), `src/ui/extraction_revive_document.cpp`
(the drawn prompt) and `src/ui/gameplay_settings_document.cpp` (the setting).

Detecting "reaches an extraction point": both modes use presence, not the Use
command. The input poll (0.25 s throttle, only while gameplay control is
active) scans the entity table for a pad and tests hero-to-pad 2D distance
against 480.0, the retail nearby-revive radius in `0x00428ce0`. Pads are
entities whose definition name starts `xtraction_point` (`xtraction_point` on 30
maps, `xtraction_point_sidemission` on 4).

- `free` is edge-triggered by `ExtractionRevive`: when any party hero comes
  within the radius it restores the party once, and not again until every hero
  (fallen ones included) has left it.
- The get-up timer end `0x004220d0` (what slot `+0x21c` queues) re-queues itself
  with a hard-coded `0.5f` and a 0.3 s delay whenever `0x005252e0` vslot `+0x1c8`
  `(actor[0x1f7], 1)` is false, so the fraction passed to the revive is lost and
  the later fire sets `max * 0.5`: a revived hero ended at 39 of 78 in about
  half the runs (live trace: fire with 1.0 keeps flags and `actor+0x60c`, then
  repeated fires with 0.5 until the check passes). `ExtractionRevive` overrides
  `0x004220d0` and writes 1.0 over the fraction argument for the actors `free`
  revived, until the fire that zeroes `actor+0x60c` (the stand-up done). What
  `0x005252e0` vslot `+0x1c8` asks is not decoded.
- `paid` offers while a hero is fallen and any hero is inside the radius.
- `extractionPoint` (`0x004a6b50`), `extractionPointLite` (`0x004a6d80`) and
  `extractionUnlock` (`0x0049f1e0`) are not touched. `0x004a6b50` keeps the save
  trace's own override.

The party and stats reads go through guest calls: party list singleton
`0x0046dce0`, slot `+0x120` (`this, buf, -1, 0`, five handles then the count at
`buf+0x14`); `0x0041fb10` is CDECL `(handle)` returning the actor or 0 (it is
not a thiscall); `0x0041d5a0` (this = actor) returns the hero record
`actor+0x35c`; record `+0xc0` is the stats object, vslot `+0x28` the dead
predicate, `+0x1c` the revive; `0x004b87c0` (this = record) the level.
Revive of one hero is the retail roster sequence (`0x005e4010`): stats revive,
actor slot `+0x1a4` with `actor+0x284`, slot `+0x21c(0, [0x0069d058], 0.5f, 1, 0)`.
`free` passes 1.0f and also fills energy (slot `+0x1a8`, max `actor+0x314`).
`paid` takes `sum(max(200, 2*level^2))` from the money object (getter vslot
`+0x24`, setter `+0x6c`) in one write and never goes below zero; a refusal is
announced, takes nothing and revives nothing.

Entity table (read by `retail_entities.cpp`, checked reads only): handle manager
`0x00778b70`, pointer table at `+4` indexed by `handle & [+0xc3c]`; entity
`+0x14` definition (string-pool handle), `+0x1c` handle, `+0x20` x, y, z. String
pool `0x00a2c440`: text at `pool + 0x4008 + [pool + 4 + (h & 0xffffff) * 4]`.

`GET /party` reports money, every party hero (health, energy, position), the
pads and the prompt's offer and notice, so a live case judges the game's own
memory.

Live cases (`tools/live_cases_extraction.py`, genosha1, pad at 2580.5, 2580.0):
`extraction-off` (a fallen hero stays down, nothing is offered),
`extraction-free` (the party is teleported into the radius with a hero fallen:
restored once on entry with no command, nothing again while inside or after
leaving, restored again on re-entry), `extraction-paid` (offer text,
money-short refusal, then F3, controller LB and a touch tap each pay the
offered total and revive). Teleports go Z last per hero; X, Y first with Z
unset drops heroes through the floor. Arriving at genosha1's pad starts a
Lady Deathstrike scene that must be skipped before control returns, and its
enemies killed a hero in about one run in four, so the cases make heroes 1, 3
and 4 `setInvulnerable('_HERON_','TRUE')` after the teleport.

## Not established

- When the lock bit clears relative to `openmenu('worldmap')`: only the steady
  state (clear while the map is up) was read, not the order of `0x004a6b50`'s
  set and `0x005eb930`'s clear.
- Whether retail PC shows the Escape defect on the world map; the guest code
  and bindings are the same, but no stock run was driven.
- The dead flag's exact field: the stats object's vslot `+0x28` predicate
  (`actor[0xc0]`), not decompiled.
- How the world-map choice leads into the roster team menu after an extraction.
- Whether `DAT_008b134c & 0x10` is multiplayer or something else.
- `free` filling energy: the call is the retail setter, but no script lowers
  energy and a power chord did not fire in the harness, so a refill from below
  maximum was not observed. Health refill (7 to maximum) was.
- The cost the port shows is `revive_cost` over the guest's level; it was
  observed as 200 per hero at level 1 and was not compared against a guest call
  of `0x004b8830`.
- The roster guard `(DAT_008b134c & 0x10) == 0` has no counterpart in `paid`.
- `/party` +8 entity flags did not distinguish dead from alive; death is
  health <= 0 there and the stats predicate in the bridge.
