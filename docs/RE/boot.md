# Boot sequence and the direct level-load path

How XMen2.exe gets from process start to the first script, how New Game builds
its party, and the path the `loadmap` console command uses to load a level --
the mechanism behind `X2_BOOT_MAP` (`src/native/startup.c`).

## The boot sequence: FUN_00402ba0

FUN_00402ba0 is the "launchMap" handler the engine fires on the INIT event. It
is called repeatedly during boot; the bits of `[this+0x28]` make each phase
run once, and a 5-second timer gate (`FUN_0055b610` is the timer singleton)
holds the whole thing until the engine has had time to settle.

```
if (now - [this+0x24] >= 5.0) {
  if (!(flags & 8)) { console->INIT-handler setup; flags |= 8; }
  if (!(flags & 4)) { FUN_004b2880()->[+0x14](1); flags |= 4; }
  if (!(flags & 2)) {
    if (FUN_004b2880()->[+0x18]() == 0 && FUN_005eb300()->[+0x78]() == 0) {
      FUN_0055ce30()->[+0x14]("INIT", "launchMap", buf, 0);   // register handler
      console->[+0x18]("resetgame");                          // reset game state
      strncpy(buf, "main", 0x40);
      if (_stricmp(buf, "main") == 0)
        console->[+0x18]("runscript menus/intro_normal");     // THE NORMAL BOOT
      else
        mapmgr->[+0x6c](buf, 0);                              // DEAD CODE: boot map
      flags |= 2;
    }
  }
}
```

The boot map name is hardcoded to `"main"`, so the `loadMap` branch is
unreachable in the shipped binary -- the game always runs
`runscript menus/intro_normal`.

## The managers and their vtables

All are singletons; the vtables are installed by each manager's init function
(they are NOT in the static image, so read them from the decompiled init):

| getter | returns | vtable | slots used here |
|---|---|---|---|
| `FUN_0055c890()` | `&DAT_007ac290` (console) | `0x0069a81c` | +0x0c `FUN_0055b6e0` (name lookup), +0x14 (register), +0x18 `FUN_0055beb0` (command executor), +0x1c `FUN_0055c410` (command line) |
| `FUN_005d8920()` | `DAT_008aff18` (map mgr) | `0x006a236c` | +0x6c `FUN_005d5db0` (queue a pending map) |
| `FUN_0055ce30()` | `&DAT_007ad2bc` (event mgr) | | +0x14 |
| `FUN_004b2880()` | `&DAT_0075cbc0` | | +0x14, +0x18 (predicates) |
| `FUN_00585e70()` | `&DAT_007de9c8` | | +0x14 |
| `FUN_005eb300()` | `DAT_008b13ec` | | +0x78 (predicate) |

## The menu script and the movie -> level script

The scripts are plain ASCII files under `Scripts/menus/`:

- `intro_normal.py` -- six `startMovie`/`waitsignal` pairs (i102, i101, i103,
  i107, i104, i105), then `mainMenuExit()`. This is what the ~2600-frame
  preamble before the main menu is.
- `main_back_main.py` -- the menu backdrop camera.
- `new_game.py` -- `startMovie("cine01")`, `waitsignal`, then
  `loadMapKeepTeam("act0/tutorial/tutorial1")` (normal difficulty).
- `new_game_hard.py` -- same but `loadMapChooseTeam` (hard).

## The `loadmap` command and how loadMapKeepTeam really works

`loadMapKeepTeam`'s handler is FUN_004a0cc0 (its registration sits at
`0x68b688`, next to the name string `0x68c020`). It builds a console command
and runs it through the console's command-line path:

```c
// FUN_004a0cc0 (loadMapKeepTeam)
uVar2 = FUN_004d5830(0)->[+0x14]();          // the script argument (map name)
sprintf(buf, "loadmap %s 0 0", uVar2);
FUN_0055c890()->[+0x1c](buf);                 // console +0x1c = FUN_0055c410
FUN_0059ee20()->[+0x80]();                    // kick the state machine
```

`loadMapChooseTeam` (FUN_004a0d30) is identical with `"loadmap %s 0 1"` -- the
third field is loadmap's team mode: `0` keep team, `1` choose team.

`FUN_0055c410` (console +0x1c) copies the string and feeds it to the command
line processor FUN_0055c100, which is where `loadmap` is a real command.

## The New Game owner: `startFirstMission`

The difficulty dialog ultimately invokes the registered BehavEd command
`startFirstMission`, FUN_004a7b10. Its command-table entry is at `0x0068b7e0`
and names the function at `0x0068be9c`. This is the required initialization
boundary; `new_game.py` is only what it launches after the state exists.

In order, FUN_004a7b10:

1. resets the game/menu manager through `FUN_004b2880()->[+0x44]()`;
2. enables New Game state through the party manager
   (`FUN_0046dce0()->[+0x280](1)`);
3. closes the difficulty dialog through `FUN_0044b8f0()->[+0xc0](1)`;
4. assigns the retail default team through party-manager slot `+0xf0`:
   Magneto, Cyclops, Wolverine and Storm in positions 0..3;
5. runs `menus/new_game`, or `menus/new_game_hard` when the selected difficulty
   is hard.

Calling `loadmap ... 0 0` before this function means “keep” a team that does
not exist. That was the root cause of the old boot-map path's zero hero handles.

## X2_BOOT_MAP -- skip presentation, preserve initialization

`src/native/startup.c` overrides `FUN_0055beb0` (console +0x18, the command
executor). With `X2_BOOT_MAP=<map>` set, the one string
`runscript menus/intro_normal` is intercepted and the retail
`startFirstMission` function is called. That function synchronously installs
the real default party and asks the console to run `menus/new_game`; only that
nested script command is replaced with `loadmap <requested map> 0 0` through
the same console +0x1c path `loadMapKeepTeam` uses. The skipped work is the six
intro movies, main menu, difficulty dialog and cine01 -- not game-state setup.
Unset or 0 remains a pure pass-through.

The old implementation replaced the intro with the bare load. C218 measured
the resulting defect: 0 of 5 hero handles resolved, and the tutorial's second
conversation was suppressed because no speaker actor existed. After the
ordering fix (C223), a live `tools/x2ctl.py input` probe measured current player 0,
handle `0x00000201 -> actor 0x08326010` (1 of 5 resolves). The same driven run
then reached the previously suppressed second conversation with flags
`0x18 -> 0x13` (speaking and visible), rather than the old `0x18 -> 0x10` with
no selected line.
