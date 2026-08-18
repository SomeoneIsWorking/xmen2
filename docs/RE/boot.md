# Boot sequence and the direct level-load path

How XMen2.exe gets from process start to the first script, and the path the
`loadmap` console command uses to load a level -- the mechanism behind the
`X2_BOOT_MAP` testing shortcut (`src/native/startup.c`).

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

## X2_BOOT_MAP -- the testing shortcut

`src/native/startup.c` overrides `FUN_0055beb0` (console +0x18, the command
executor). With `X2_BOOT_MAP=<map>` set, the one string
`runscript menus/intro_normal` is intercepted and `loadmap <map> 0 0` is run
through the SAME console +0x1c path `loadMapKeepTeam` uses -- the engine has
already run `resetgame` by then, because the boot does that before the script.
Unset or 0: pass-through, the boot is untouched.

Measured (C208): the tutorial opens at frame 33, 0 draws refused, clean exit;
the normal smoke path needs ~4200 frames and a six-press input script.

Note the one wrinkle the static chain cannot answer: `loadmap ... 0 0`
(keep-team) with no party built. Empirically the tutorial loads anyway, but a
fresh level that requires a chosen team may need `0 1` or a party built first.