# The retail front end: where the menus live and how they are hit

Every menu in this game is `XMen2.exe`'s own `CMenu` system, not the Alchemy
GUI. Nothing in `libIGGui.dll` is used: the executable does not import it, and
neither does any other `libIG*.dll` in the PC install. The `igGuiSystem`,
`igGuiComponent` and `_componentUnderMouse` strings that read like a widget tree
belong to a DLL that never loads.

Addresses below are the linked `XMen2.exe` addresses. Menu layout data was read
from the player's own install (`UI/menus/*.XMLB`) with `tools/xmlb.py`; the code
was read with Ghidra 12.0.4 through pyghidra against a copy of `build/ghidra`.

## The classes

The executable's RTTI names 33 menu classes and one manager: `CMenu`,
`CMenuMgr`, `IMenuMgr`, and `CMenuMain`, `CMenuOptions`,
`CMenuOptionsController`, `CMenuPDA`, `CMenuTeam`, `CMenuWorldMap`,
`CMenuShop`, `CMenuCodex`, `CMenuTextEntry`, `CMenuMovie`, `CMenuLoading`,
`CMenuJoin`, `CMenuHost`, `CMenuPlayersList`, `CMenuGamesList`, `CMenuHeroList`,
`CMenuDangerRoom`, `CMenuTrivia`, `CMenuReviewPaths`, `CMenuCampaignLobby`,
`CMenuOnline`, `CMenuPlayers`, `CMenuPersonal`, `CMenuPlayerGameOptions`,
`CMenuImageViewer`, `CMenuCredits`, `CMenuRegion`, `CMenuAutoMap`,
`CMenuSebas`, `CMenuListItem`, and the item types.

`CMenu` owns 144 slots in its vtable, one per menu class, each a 33-entry table
whose slots 0, 0x10 and 0x3c onwards are shared. The shared slots identify the
base class:

| slot | function | what it is |
|---|---|---|
| `+0x00` | `FUN_005afa90`… per class | the class constructor |
| `+0x04` | `FUN_005aaf80` | shared; `CMenu::setFocus` is called through it |
| `+0x44` | per class | `CMenuItem::parse`, called on the item the factory just built |
| `+0x48` | `FUN_005af5a0` | shared; frees every registered item and zeroes `menu+0x1608` |
| `+0x4c` | `FUN_005ab000` | parses the `items` child |
| **`+0x50`** | **`FUN_005af490`** | **per `<item>`: the `0xaf` gate, the factory call, the name registration** |
| `+0x54` | `FUN_005aead0` | parses `itemlink` children |
| `+0x5c` | `FUN_005ad830` | parses the menu itself |
| `+0x68` | `FUN_005ae6b0` | shared |
| `+0x80` | per class | **the mouse handler, `CMenu::onMouse`** |

`CMenuMgr` is the singleton `FUN_005d8920`, stored at `DAT_008aff18` and
created by `FUN_005d88b0`. Its vtable is `PTR_FUN_006a236c` (144 entries);
`+0x210` is `FUN_005d83d0`, which is `MOV EAX,[ECX+0x86090]; RET` — the
**active menu is a field read at `CMenuMgr+0x86090`**, not a computation.

## Where a menu's rows are

A menu's data is `UI/menus/<name>.igb` for the art and `UI/menus/<name>.xmlb`
for the layout. The XMLB is a flat tree of `<MENU>`, `<items>` and `<item>`
elements; the strings `ui/menus/%s.igb` and `ui/menus/%s.xmlb` are at
`0x69e228` and `0x69e244`, formatted by `FUN_005ab130` and read at `0x005ab151`.

`FUN_005bf220` is the item factory. It matches the `type` attribute against
fourteen names — `MENU_ITEM_TEXT`, `MENU_ITEM_MODEL`, `MENU_ITEM_LIST`,
`MENU_ITEM_LIST_CYCLE`, `MENU_ITEM_LIST_ITEMS`, `MENU_ITEM_BINARY`,
`MENU_ITEM_LISTBOX`, `MENU_ITEM_LISTCODEX`, `MENU_ITEM_TEXTBOX`,
`MENU_ITEM_ACTORMODEL`, `MENU_ITEM_EFFECT`, `MENU_ITEM_CHAR_SUMMARY`,
`MENU_ITEM_LISTCHARS`, `MENU_ITEM_BAR`, `MENU_ITEM_SKILLS` — and calls the
matching class's constructor. It refuses outright once the count reaches `0xaf`
(`MOV EAX,[EBP+0x22898]` / `CMP EAX,0xaf` at `0x005bf239`/`0x005bf242`), and so
does the caller that gates it (`CMP DWORD PTR [EBX+0x1608],0xaf` at
`0x005af4af`), so **no menu holds more than 175 items**.

`CMenu::itemByName` is `FUN_005adc10(menu, name)`. It hashes the name
(`FUN_005ab780`, case-folding at `FUN_00602120`), looks it up in the menu's own
table (`FUN_005ab7d0`, a 16-byte-node tree) and returns the pointer at
`menu + 0x160c + index*4`. That array is the menu's rows.

## The item count: `CMenu + 0x1608`

**The count that belongs to the `CMenu+0x160c` array is `CMenu + 0x1608`, an
int32.** It is not on the manager, and `menu + 0x22898` is not it.

The maintaining path, end to end. `FUN_005af490` is the shared `CMenu` vtable
entry at `+0x50` (every menu class's table carries the same pointer; the base
table `PTR_FUN_0069e41c` has it at `0x0069e46c`), and it is the whole per-item
lifecycle:

```
005af4a6  CALL 0x005649c0                    ; the <item> element
005af4af  CMP DWORD PTR [EBX+0x1608],0xaf    ; refuse past 175
005af4b9  JNZ  0x005af4c5
005af4dd  CALL 0x005d8920                    ; CMenuMgr
005af4e7  CALL DWORD PTR [EDX+0x88]          ; manager vtable +0x88 =
005af4e9                                      ; 0x005d4d70: ADD ECX,0x867ac
                                            ; JMP 0x005bf220  -> the item
005af4f7  MOV DWORD PTR [ESI+0x4],EBX        ; item+0x04 = the CMenu
005af4ff  CALL DWORD PTR [EAX+0x44]          ; item->parse(element)
005af504  LEA EDI,[EBX+0x824]                ; the menu's name list
005af56a  CALL 0x005aefa0                    ; register the item's name
```

`FUN_005aefa0` is called with `this = menu + 0x824` and is the only writer of
the array in the whole image:

```
005aefa0  MOV ESI,ECX                  ; ESI = menu+0x824
005aefa4  LEA EDI,[ESI+0xc]            ; EDI = menu+0x830
005aefa9  CALL 0x005ab870              ; -> menu+0x830, the NAME-slot allocator
005aefbd  MOV DWORD PTR [ESI+8],EAX    ; list head = the slot just taken
005af017  MOV ECX,DWORD PTR [ESI+8]
005af01d  MOV DWORD PTR [ESI+ECX*4+0xde8],EDX
                                          ; menu+0x824+idx*4+0xde8
                                          ;   = menu+0x160c+idx*4 = the item
```

`FUN_005ab870(this = menu + 0x830)` is the same shape as the manager pool's
`FUN_005bedc0`, every offset 0x830 nearer the base:

```
005ab872  MOV EAX,[EDX+0xdb8]                 ; slot cursor
005ab878  MOV EAX,[EDX+EAX*4+0xaf4]           ; free-slot map
005ab899  OR  DWORD PTR [ESI],EDI             ; [EDX+0xdc0] |= 1<<slot
005ab89b  MOV ESI,[EDX+0xdb8]
005ab8a1  INC ESI
005ab8a4  CMP ECX,0xaf                        ; 175
005ab8aa  MOV DWORD PTR [EDX+0xdb8],ESI       ; cursor++
005ab8b2  MOV DWORD PTR [EDX+0xdb8],0x0       ; wraps at 175
005ab8bc  DEC DWORD PTR [EDX+0xdbc]           ; free count--
005ab8c2  MOV ECX,[EDX+0xdd8]
005ab8c9  INC ECX
005ab8ca  MOV DWORD PTR [EDX+0xdd8],ECX       ; *** menu+0x830+0xdd8
                                          ***     = CMenu + 0x1608  += 1
```

`0x830 + 0xdd8 = 0x1608`. It also lands where a 175-bit field must: the
allocated bitset is six dwords at `menu+0x15f0` (`FUN_005af5a0` zeroes
`+0x00..+0x14`), and `0x15f0 + 6*4 = 0x1608` is the count, with the array
starting on the next dword at `0x160c`.
The count is written by exactly one instruction in
the whole image (`0x005ab8ca`), and `0x1608` appears as a displacement exactly
twice — the two `CMP`s that read it, `0x005ad1f3` (`CMenu::setFocusByName`'s
"are there any items" test, `CMP DWORD PTR [ESI+0x1608],EDI` / `JLE`) and
`0x005af4af` (the `0xaf` refusal above). Its `0xde4` alias off a `menu+0x824`
base appears zero times, which is what a field with one writer and two readers
looks like.

The zero side is `FUN_005af5a0`, the `CMenu` vtable entry at `+0x48` — the item
reset. It walks the same list and frees every registered item through the
manager (`CMenuMgr` vtable `+0x8c`, `0x005d4d80: ADD ECX,0x867ac; JMP
0x005bee90`), then:

```
005af5b0  MOV DWORD PTR [ESI+0x324],0x0        ; clear the focus
005af601  MOV ECX,[EDI+EBP*4+0xde8]            ; menu+0x160c+i*4
005af60d  CALL DWORD PTR [EDX+0x8c]            ; CMenuMgr->freeItem(item)
005af615  MOV DWORD PTR [EDI+0x4],0x3fffffff   ; menu+0x828 = list end
005af61e  LEA ECX,[EDI+0xc]                    ; ECX = menu+0x830
005af621  LEA EAX,[ECX+0xdc0]                  ; EAX = menu+0x15f0
005af629  MOV DWORD PTR [ECX+0xdd8],EBP        ; menu+0x1608 = 0   (COUNT)
005af62f  MOV DWORD PTR [EAX+0x0],EDX          ; menu+0x15f0..0x1604 = 0
...
005af63d  MOV DWORD PTR [EAX+0x14],EDX
005af640  CALL 0x005ac410                      ; rebuild the free list
```

`FUN_005ac410(menu+0x830)` is the reset-side builder, the mirror image of the
manager's `FUN_005d7380`: it zeroes `menu+0x15e8` / `menu+0x15ec`, then fills
`menu + 0x1324 + i*4 = i` for `i` in `0..0xae`, leaving `menu+0x15ec = 175`.

### The menu's name-slot allocator, in full

All offsets from the `CMenu`:

| offset | meaning |
|---|---|
| `+0x0c` | the 64-byte name the menu was opened by |
| `+0x324` | the focused `CMenuItem*` |
| `+0x824` | the name/list pool object: `+4` node free cursor (`0x3fffffff` when empty), `+8` head index, `+0xc` the 16-byte-node arena (which is `menu+0x830`); its `FUN_005aefa0` path writes `this+0xde8 + idx*4` = `menu+0x160c+idx*4` |
| `+0x1324` | `int[175]` free-slot → slot-number map (`menu+0x830+0xaf4`) |
| `+0x15e8` | slot cursor (`menu+0x830+0xdb8`) |
| `+0x15ec` | free slot count; 175 when empty (`menu+0x830+0xdbc`) |
| `+0x15f0` | `uint32[6]` name-slot allocated bitset, slot `i` = `0x15f0+(i>>5)*4` bit `i&31` |
| **`+0x1608`** | **the item count: allocated name slots** |
| **`+0x160c`** | **`CMenuItem*` `[175]`, indexed by name-slot index** |

### The manager's item-object pool is a different pool

`CMenuMgr + 0x867ac` is real, and it is where the `CMenuItem` **objects** live —
inline, stride `0x324`, at `pool + slot*0x324`:

```
005bf239  MOV EAX,[EBP+0x22898]        ; this pool's count; refuse at 0xaf
005bf242  CMP EAX,0xaf
005bf262  CALL 0x005bedc0              ; this pool's slot allocator
005bf26c  MOV EBX,[EBP+ECX*4+0x2259c]  ; live bitset, OR in 1<<slot
005bf27c  IMUL EAX,EAX,0x324
005bf28c  ADD EAX,EBP                   ; EAX = EBP + slot*0x324 = the item
```

`pool+0x22898` is that pool's live item count: `FUN_005bedc0` increments it,
`FUN_005beca0(pool, slot)` (the free) decrements it. `pool+0x2259c` and
`pool+0x22880` are two bitsets of *object* slots and `pool+0x225b4` is their
free map.

**The array index and the object slot come from two different allocators.**
`menu+0x160c[i]`'s index is a *name slot* handed out by `FUN_005ab870` from the
menu's own free list (`menu+0x1324`), while the pointer it holds is whatever
*object* slot the manager's `FUN_005bedc0` returned from the pool's free list
(`pool+0x225b4`). In the measured run the two coincided — every `main` and
`options` entry sat at `array[0] + i*0x324` — because both free lists are filled
0..174 in order by `FUN_005ac410` and `FUN_005d7380` and both were still
untouched when those menus were built. That is a coincidence of a fresh boot,
not an invariant: the two lists are returned to in different orders
(`FUN_005af5a0` frees items walking the name list, `FUN_00601a40` frees name
slots from the list's own cursor). A reader must not gate a walk of
`menu+0x160c` on `pool+0x2259c`'s bit `i`, and must not compute
`item = pool + i*0x324`.

### Why the reader saw zero

`CMenuMgr + 0x867ac + 0x22898` is the manager pool's **global** live-item
total, not the active menu's count, and it reads **0 whenever the active menu
owns no items** — which is exactly when the intro is up. On a cold boot the
active menu for most of the first minute is `CMenuMovie`: `movie.XMLB` has
**zero** `<item>` elements and `FUN_005af5a0` had already reset it, so both
`menu+0x1608` and `CMenuMgr+0x867ac+0x22898` are 0 while a menu is genuinely up.
That field tracks the current menu only by accident — the previous menu's items
are freed when the next one opens — so it is right on `main` and wrong on
`movie`.

The pool count also leaves a **stale pointer** behind: `FUN_005af5a0` frees the
items and zeroes the count but never clears `menu+0x160c`, so a menu with count
0 can still have non-NULL entries in the array (observed: `movie` with
`+0x1608 = 0` and `+0x160c[0] = 0x27105b7c`, an item whose `item+0x04` still
named the menu). `menu+0x15f0`'s bitset is the one gate that is in the right
index space, because it is indexed by the same name-slot index as the array.

That stale entry is also the likeliest reading behind the earlier "`legal_pc`
once returned 2 rows": `legal_pc.xmlb` contains exactly **one** `<item>`
(`label_legal`, a `MENU_ITEM_TEXTBOX`), so a second row came from past the end
of the count, not from the package.

`CMenu` objects are themselves pooled and reused: in one run `main` and
`options` were the **same** address (`0x27128534`) under the same manager, with
the name at `+0x0c` the only thing that told them apart. A reader must
re-read after every change rather than cache a menu.

### Measured

Read through `/proc/<pid>/mem` on a real headless run
(`--no-window --d3d8 --unbounded`, main menu reached by key), against the
`<item>` count of the shipped package read with `tools/xmlb.py`:

| active menu | `menu+0x1608` | `<item>` in its XMLB | `menu+0x15ec` | `menu+0x15f0` | `menu+0x160c` |
|---|---|---|---|---|---|
| `movie` | 0 | 0 | 175 | 0 | stale `[0]`, `0x27105b7c` |
| `main` | **42** | **42** | 133 = 175−42 | `0xffffffff` | dense 0..41, `0` at 42 |
| `options` | **73** | **73** | 102 = 175−73 | `0xffffffff` | dense 0..72, `0` at 73 |

`main+0x160c[i].item+0x04` named the active menu for every populated entry, and
every populated entry had a non-zero hit rect. The `6`/`10`/`8`/`9` figures
this document's table gives are **pressable text rows**, not items: `main`
holds 42 items of which 6 are `label_option04..09`.

## How a row becomes pressable

`CMenuItem::updateHitRect` is `FUN_005bc530`, at `CMenuItem` vtable `+0x64`. It
takes the item's world bounding box, converts each axis through
`FUN_0067217c`, and writes **signed 16-bit** fields:

| field | meaning |
|---|---|
| `item+0x70` | box left |
| `item+0x72` | box top |
| `item+0x74` | box width |
| `item+0x76` | box height |
| `item+0x78` | box half-extent, x |
| `item+0x7a` | box half-extent, z |
| `item+0x7c` | depth extent |

`CMenuItem::onMouse` is `FUN_005bc1b0`. It rebuilds a `RECT` from those fields —
`left = [0x70]`, `right = left + [0x74] - 1`, `bottom = [0x72] + [0x7a]/2`,
`top = bottom - [0x76] - 1` — and runs `PtInRect` against the pointer. On a hit
with a non-empty command it publishes **`MENU_ACCEPT` (action 4)**. It does
nothing at all when `item+0x24` (the hashed `usecmd`) is empty, which is why a
drawn row with no command behind it is not something a contact can choose.

`CMenuItemText::onMouse` is `FUN_005c6490`, at the same slot in the text item's
table. Same box, plus the text-specific case: when the item's text contains
`$MENU_OK`, `$MENU_BACK`, `$MENU_NEXT`, `$MENU_PREV`, `$MENU_OTHER`,
`$MENU_ACCEPT` or `$MENU_SUBTRACT`, a `WM_LBUTTONUP` (`0x202`) on it writes that
action into `DAT_00a09f54[player]` and sets `DAT_00a09fa0 = 1`:

| token | action |
|---|---|
| `$MENU_ACCEPT` | 4 |
| `$MENU_SUBTRACT` | 8 |
| `$MENU_OK` | 0x14 |
| `$MENU_BACK` | 0x15 |
| `$MENU_NEXT` | 0x16 |
| `$MENU_PREV` | 0x17 |
| `$MENU_OTHER` | 0x18 |

The footer prompts are the menu's `desctext1..5` text items, so a click inside
the one carrying `$MENU_BACK` is the game's own Back.

`CMenu::onMouse` is `FUN_005ae0a0`. It walks the menu's focus links
(`FUN_00585be0`/`FUN_00585c10` over the chain at `menu+0x824`), skips items
that are hidden or disabled (`item+0x54 & 8` is the enabled bit both this and
`FUN_005ae990` require), asks each surviving item's `+0x74`, and on the first hit
calls `setFocus` on it (unless it is `neverfocus`) and `CMenuMgr+0xe8(0)`. It does
this for every mouse message, so a move alone focuses a row. Because the item's
`+0x74` hits only with a `usecmd`, a row without one (`danger room`, `play
online`, the volume rows) is never focused or chosen by the mouse.

Two other consumers of the same fields exist and are **not** menu rows:
`FUN_005ae490` and `FUN_005ae990` read `item+0x54 & 0x20` (a link target, so
never a legal fallback focus) and `CMenu::setFocus`'s slot, and the HUD's
`FUN_005f9eb0` reads `item+0x70..0x76` for the portrait, potion and menu-icon
boxes.

## Where the pointer comes from

`FUN_005f9eb0` is the retail mouse handler, reached from
`igWin32Window::getEvents`. It converts the Win32 client point into the scene
plane and then branches, in this order:

1. a popup is up → `popup+0x7c(mouse_x, mouse_y, msg, wparam, lparam)`;
2. **`CMenuMgr+0x204()` says a menu is up → `menu+0x80(...)`**, which is
   `CMenu::onMouse`;
3. otherwise `FUN_004583f0` for the automap/in-game overlays;
4. otherwise the gameplay HUD boxes: portraits at `00a0a0cc+slot*12` ±20,
   potions at `00a0a0bc` and `00a0a118` ±`[0x682cc0]`, and the two menu icons at
   `00a0a10c`/`00a0a114` and `00a0a124`/`00a0a12c` as `[0x683ffc]` squares.

So a mouse already reaches a menu row through exactly one call —
`CMenu::onMouse` — and that is the whole of what touch has to satisfy.

## The scene plane, and the two mappings into it

The scene plane is the viewport singleton `FUN_005f6df0` at `00a0a138`, vtable
`006a3a9c` (see [HUD](hud.md)). `x2_hud_space` already reconstructs it:
`width = scaleX(+0x48) * aspect(+0x10) * 384`, `height = scaleZ(+0x4c) * 384`,
`left = 256 - width/2`, `top = 192 + height/2`.

`FUN_005f9eb0` maps a client point into it with **integer** arithmetic against
two globals it does not derive:

```
scene_x = viewport_left + (client_x * viewport_width  / DAT_00a09ffc)
scene_z = viewport_height - (client_z * viewport_height / DAT_00a0a000)
```

`DAT_00a09ffc` and `DAT_00a0a000` are the logical backbuffer the game is
presenting. Both are read, never assumed, so the mapping is the presenter's own.
`x2::presentation::RetailScenePlane` reproduces this truncating arithmetic and
its inverse; the touch menu uses the inverse to place a click inside a row's hit
box.

## The touch menu's delivery

The touch menu (see [Touch play](../touch-play.md)) drives only the paths above.
A row with a `usecmd` gets a click at the centre of its hit box, converted to
client space by the inverse mapping; `CMenu::onMouse` focuses it and the item
publishes `MENU_ACCEPT`. A footer gets a click on its `desctext` item. A row
without a `usecmd`, and a left/right step, cannot be reached by the mouse, so
the touch menu presses the virtual pad's Up or Down (the shorter way round the
game's own order) until the model's focused row is the target, then A, Left or
Right. The pad's d-pad release is held until the game has read the pressed
state, as its buttons already were; a release in the same frame was never seen.

## The menu model: what a reader can recover from memory

`x2::menu::RetailMenuModel` (`src/native/retail_menu_model.cpp`) reads the
active menu into a plain snapshot through checked reads only, and `GET /menu`
serves it. Everything below was read from the code named and checked live on
`main`, `options` and `pda`.

### Manager, menu and item array

| address / offset | what it is | evidence |
|---|---|---|
| `DAT_008aff18` | `CMenuMgr*` | `FUN_005d88b0` stores it |
| `mgr+0x86090` | active `CMenu*` | `FUN_005d83d0` |
| `menu+0x0c` | menu name, 64 bytes | `FUN_005ad830` |
| `menu+0xec`, `+0xf0` | `opencmd`, `closecmd` (pool-2 handles) | `FUN_005ad830` |
| `menu+0x2f4` | `desctext[5]` handles | `FUN_005bc6d0`, called from `FUN_005ad830` |
| `menu+0x324` | focused item index | `CMenu::setFocus` (`FUN_005aaf80`); reads 0 for a frame right after open |
| `menu+0x824` | name tree: root `+4`, node *i* at `+0xc+i*16` (parent, left, right, …, key `+0x18`) | `FUN_005ab7d0` |
| `menu+0x15f0` | live-slot bitset over the item array | `FUN_005af490`, `FUN_005af5a0` |
| `menu+0x1608` | item count (≤ `0xaf`) | see above |
| `menu+0x160c` | `CMenuItem*[0xaf]`; a released slot keeps a stale pointer, so the bitset decides | `FUN_005adc10` |

The menu's class is its vtable, named through RTTI (`CMenuMain` `0x69f134`,
`CMenuOptions` `0x69ebd4`, `CMenuPDA` `0x69ed6c`, plain `CMenu` `0x69e41c`, 30
in all; the table is in the model source).

### The item (`CMenuItem`, constructor `FUN_005bd860`, parse `FUN_005bc7a0`)

| offset | what it is |
|---|---|
| `+0x00` | vtable → class through RTTI (`CMenuItem` `0x6a007c`, `Text` `0x6a10cc`, `TextBox` `0x6a1154`, `Model` `0x6a0304`, `Bar` `0x6a042c`, `Binary` `0x6a04b4`, `List` `0x6a053c`, `ListBox` `0x6a062c`, `ListCycle` `0x6a0914`, …) |
| `+0x08` | `name` (pool-2 handle) |
| `+0x0c..+0x1c` | `desctext[5]` |
| `+0x24` | `usecmd` |
| `+0x28`, `+0x2c` | `leftcmd`, `rightcmd` |
| `+0x30` | `animtext_scene` |
| `+0x34`, `+0x38` | gamevar getter and setter, copied from the registry by name |
| `+0x40` | bar fill, 0..1 (drawn by `FUN_005bdac0`); `+0x44` its step, 0.1 |
| `+0x54` | flags: `0x01` focus-lit (observed), `0x02` `startactive`, `0x04` hidden (vtable slot 1, `FUN_005bd8f0`), `0x08` enabled, `0x10` skipped by navigation, `0x20` `neverfocus`, `0x40` `menu_ok`, `0x80` `menu_cancel` |
| `+0x55` | `0x01` build/platform hidden, `0x02` animate, `0x04` debug hidden |
| `+0x56`, `+0x57` | filter, mode bitmask |
| `+0x58` | model |
| `+0x5c/+0x60/+0x64/+0x68` | left/right/up/down links, item pointers; set from `itemlink` by `FUN_005bbc30`/`FUN_005aec00`, the unset ones filled spatially by `FUN_005ad2f0` |
| `+0x6c` | `fielditem` |
| `+0x70..+0x7c` | the hit box above |

`0x20` is the `neverfocus` attribute, parsed into this bit by `FUN_005bc7a0`;
the "link target" reading above is the same bit seen from `FUN_005ae990`.

### Label text

| class | text | evidence |
|---|---|---|
| `CMenuItemText` and everything whose `getText` is `FUN_005c67f0` (`CharSummary`, `Bar`, `Binary`, all `List*`, `Skills`) | dynamic slot `+0xa0` ≥ 0 → the 32-byte buffer at `0x8adab8 + slot*0x20` when bit `slot` of `0x8add9c` is set (20 slots, `FUN_005c6720`); else pool-2 handle `+0x9c` | `FUN_005c67f0`, `FUN_005c6850` (setText) |
| `CMenuItemTextBox` | `char*` at `+0xb0`, else `+0xac` | `FUN_005c7320` |
| `CMenuItemBinary` | option handles `+0xac`, `+0xb0` | its parse |

String pools: pool 2 is `*0xa0a81c`; a handle `h` resolves to
`pool + 0x1008 + pool[1 + (h & 0xffffff)]`, capacity `0x1c00`, 0x400 slots,
hash `FUN_0041a460`. Pool 0 is static at `0xa0a820`: 0x2000 slots, strings at
`+0x8008`, capacity `0x14400` (`0x0041a387`). `pool[0]` changes per menu (4,
then 5 observed); resolution ignores it. Text is Latin-1; the model converts it
to UTF-8.

### Navigation order

`CMenuItem::nextInDirection` is `FUN_005bccd0`. From an item it follows one link,
skipping any item with `flags & 0x14` or without `flags & 8`, keeping a
32-entry visited list; on a loop it falls back to the latest eligible visited
item, else the start. The model ports it and orders rows by anchoring on the
`startactive` navigable item, walking up to the head, then down. Default focus
is `FUN_005ae990`.

### Values beside rows

A row's value (Options' "On", a volume bar) is a separate item. The registry at
`0x7acad0` (`FUN_0055c8d0`; keys at `reg+4+0x18+i*16`, 0x46 nodes; getters at
`reg+0x5ac+i*8`, setters `+0x5b0`, `FUN_0055c020`/`FUN_0055c090`) maps the last
word of the row's command to a gamevar; the item whose `+0x34` holds that
getter is the row's value. This pairing is the model's rule, not a retail one.

### The popup

`CPopupDialog` is the singleton `*0x8b13ec` (`FUN_005eb300`). `isUp` is
`FUN_005e9e30`: index `[p+0x403c]`, used only if 0..2 (else 0), then
`[p + 0x18 + idx*0x1560 + 0x155d] & 1`. Live: set while Load Game's "no saves"
popup covered the PDA, clear after dismissing it. Load Game is this popup, not
a `CMenu`.

### Measured

- `main`, fresh profile: 7 rows, `new game` … `play online` plus `Quit`
  (`debug_text`, `usecmd` `Bidon`). `danger room` and `play online` have no
  `usecmd`; `CMenuMain` acts on them itself.
- `main` with a save: `CMenuMain` rewrites the labels and commands to
  `Continue` (`loadgame`), `new game`, `load game`, `danger room`, `review`,
  `options`, `Quit`, and drops `play online`.
- `options`: 10 rows; volume bars 1.0 and 0.8, the rest paired with their value
  text. Down presses visit them in the model's order.
- `pda` (Escape in a level): 9 rows, `Objectives` … `Quit Game`; Down visits
  rows 1..8 then 0.
- `openmenu pause`: a plain `CMenu` with `button1..9`, every link 0, nothing
  drawn on PC (no `menu_pause` in the igb). The PC in-game menu is `pda`.

## The shipped screens

Every `UI/menus/*.XMLB` root and its `type`, read from the install:

| package | `type` | rows a finger chooses |
|---|---|---|
| `main` | `MAIN_MENU` | 6 text rows (`label_option04..09`), an up/down chain, `usecmd` each |
| `options` | `OPTIONS_MENU` | 10 text rows; volumes carry `leftcmd`/`rightcmd`, others `usecmd` |
| `options_controller` | `OPTIONS_CONTROLLER_MENU` | label pairs only, no `usecmd` |
| `pause`, `pause_dr`, `pause_dr_training` | `PAUSE_MENU` | 8 model rows (`button1..8`) with `focusitemname` and `usecmd` |
| `pda` | `PDA_MENU` | 9 text rows, two of them with no `usecmd` |
| `team` | `TEAM_MENU` | model rows, a `MENU_ITEM_LISTCHARS` roster, `MENU_ITEM_CHAR_SUMMARY`, `MENU_ITEM_BAR` |
| `worldmap` | `WORLD_MAP_MENU` | `MENU_ITEM_LIST_CYCLE` map list, `MENU_ITEM_TEXTBOX` description |
| `shop`, `stash` | `SHOP_MENU` | list-driven |
| `codex`, `trivia`, `review`, `region`, `automap`, `image_viewer` | their own | list-driven |
| `danger_room`, `campaign_lobby`, `host`, `join`, `players_list`, `games_list`, `hero_list`, `online`, `personal`, `players`, `player_game_options` | their own | list-driven |
| `loading`, `movie`, `credits`, `text_entry`, `sebas`, `mmlost` | their own | presentation |
| `legal_pc`, `blank`, `debug` | — | |

Three of these are worth naming because they are what a phone player meets
first: `main` is the title screen and `pda` is the in-game menu. `pause` is an
Xbox leftover: opened by hand it builds a plain `CMenu` that draws nothing on PC.

## The team menu (`CMenuTeam`)

`CMenuTeam` (vtable `0x6a2c94`) keeps its own screen state at
`menu+0x18d8`, which `CMenuTeam::onMouse` (`FUN_005e25c0`, vtable `+0x80`)
switches on:

| mode | screen | what a click does |
|---|---|---|
| 0 | the party | on `WM_LBUTTONUP`/`WM_RBUTTONUP`: a click on `char_summary01..04` selects that hero (`FUN_005e22a0` with that pad's direction); a click on the selected hero publishes `MENU_OTHER` (0x18), the details. A click on a floor pad does the same by fixed scene boxes. Then the base `CMenu::onMouse` runs, so the `desctext` footers work as in any menu |
| 1 | the roster | over `roster_summary01..03` and `roster_portrait01..03`, `WM_LBUTTONDOWN` publishes 0xb on the first entry, 0xc on the third, `MENU_OTHER` on the middle summary and `MENU_ACCEPT` on the middle portrait; the wheel publishes 0xb up and 0xc down |
| 2 | stats | a click on `body`/`focus`/`strike`/`speed` (or its label) moves to that stat; on the current stat, left-button-down or wheel up adds a point (action 4), right-button-down or wheel down removes one (8) |
| 3, 6 | skills | `skill_list` rows through the base `onMouse`; the icon strip left of the list assigns (0xd) |
| 4 | gear | `equipment` and `equipment_inv` list boxes take the click through their own `+0x74` |
| 5 | ai | the seven `label_ai_*` rows: a click moves to a row, a click on the current row accepts it |

In modes 2..6 a click on `detail_option01..04_text` (stats, skills, gear, ai)
switches tab through `menu+0x40` with the tab delta.

The party screen's items: `char_summary01..04` (`CMenuItemCharSummary`, the
hero's name as text, bit 0 of `item+0x54` lit on the selected hero),
`pad01..04` floor models, the potion and money counters, `desctext4`
`$MENU_OTHER Details` and `desctext5` `$MENU_OK Accept`. The up/down chain runs
through the potion icons (`item_health` is the menu's focus), not the heroes,
so the heroes are found by name. The roster and every detail panel are hidden
items (`item+0x54` 0x04) while the party is shown. The party screen does not
scroll.

Measured (Continue into `act2/jungle/jungle1`, PDA, Team Management): mode 0
with Magneto, Cyclops, Wolverine, Storm; a click on Cyclops lit
`char_summary02`, a second click set mode 2 and showed `details_panel`; B
returned to mode 0, and `$MENU_OK Accept` closed the team menu to gameplay.

## The shop (`CMenuShop`) and its list box (`CMenuItemListBox`)

`CMenuShop` (vtable `0x69eb4c`) serves both `shop` and `stash`; bit 0 of
`menu+0x18e8` picks the stash. Its `onMouse` (`FUN_005d3400`) runs on
`WM_LBUTTONUP`/`WM_RBUTTONUP` (or while `FUN_0061a600` reports the button
held): it finds the tabs `shop_option01..03` (`stash_option01..03` for the
stash), takes the lit one (`item+0x54` bit 0, through item vfunc `+0x50`) and
the one under the pointer, tested with the raw box (`top = [0x72] - [0x76] - 1`,
`bottom = [0x72]`, no half-depth lift). A click on a different tab publishes
axis 0 (`DAT_00a09fa8[p] = 1`, `DAT_00a09f2c[p] = 0`) at +1.0, or -1.0 when the
clicked tab is the one before the lit one; with three tabs that reaches any tab
in one step. Otherwise it runs the base `CMenu::onMouse`, and when that takes
nothing, `FUN_005d30d0`.

The shop's items: `shop_option01..03` (`buy`, `sell`, `training`), `list` (the
list box), `item_desc` (text box), `item_cost_value`, `inventory_count`,
`money_value`, `up_arrow`/`down_arrow`, and `desctext2` `$MENU_ACCEPT Buy`,
`desctext3` `$MENU_OK Accept`. Init (`FUN_005d39c0`) opens the first tab
(buy, or stash for the stash); measured from the console's `openmenu shop`,
with touch play on and off, the lit tab then stepped one back (to training,
or to inventory) about 70 ms later, but in two loaded concurrent runs it stayed on
buy. What publishes that step is not established.

`CMenuItemListBox` (vtable `0x6a062c`) fields:

| field | meaning |
|---|---|
| `item+0xac` (i16) | selected entry, -1 for none (`FUN_005bf8a0` clamps it) |
| `item+0xbc` | the entry store: one byte record id per entry, the count at `store+0x84` (`FUN_005bf750`) |
| `item+0xd8` (i16) | first entry in the window |
| `item+0xdb` (u8) | rows the window holds (vfunc `+0xec`, `0x005bf100`) |
| `item+0xdd` (u8) | rows kept between the selection and the window's edge |
| `item+0xe0` (i32) | row height, scene units |
| `item+0x2a4` | bit 1: skip entries vfunc `+0x88` refuses; bit 3: Up/Down wraps; bit 2: wordwrap |

An entry's text is a record in one table every list box shares:
`0x8a83f4 + id * 0x70` (256 records). The entry getter (vfunc `+0xe4`,
`0x005c23c0`) copies at most 0x3f bytes of it and cuts at the first tab; the
rest of the line is further columns (`columns` attribute, at most 4), which
the model reads up to the record's handle at `+0x58` as the entry's value.

The list's `onMouse` (vfunc `+0x74`, `0x005c0e10`) tests the raw box too (no
half-depth lift: for `list` the base box is 48 units higher than what it
tests). The row under the pointer is `([0x72] - y) / [0xe0]`, counted down from
the top edge, and its entry is `top + row`:

- the wheel publishes axis 1 at +1.0 (up) or -1.0 (down): one step of the
  selection, the same as the pad's Up/Down (`FUN_005c1740`, vfunc `+0x58`),
  which moves `top` when the selection leaves the window;
- a button-down on an entry that is not selected publishes one axis 1 step
  toward it and keeps the remaining steps at `item+0x88` (timed at `+0x8c`), so
  the selection walks there;
- a button-up on the entry the same button went down on, when that press did
  not move the selection (`item+0x90` clear), publishes `MENU_ACCEPT` (4) for
  the left button and 8 for the right. So a click on the selected entry buys.

While the shop's or the stash's selected entry is gear, clicks on the list
are lost: measured in the jungle with `Abnormal Waistband` selected, a click
on `Energy Pack` two rows up published one axis 1 step and the queued second
step (`item+0x88`) was dropped, a further click published nothing, and a
click on the selected gear never bought or stored it; with a pack selected
the same clicks walked and bought. The tabs, the footers and the keyboard's
Up/Down and Enter kept working. Why is not established: the list's store
(`item+0xbc`) stayed the same, and the published axis slot (`DAT_00a09fa8`)
was not overwritten between frames.

What the shop shows beside its list is written by its selection update
(`FUN_005d30d0`, run after every `onMouse` the base menu does not take): it
blanks `label_cost`, `item_cost_value`, `item_desc`, `label_owner`,
`inventory_count` and `label_inventory_count`, then, for the selected entry
only, writes the description into `item_desc`, the owner or limit line into
`label_owner` (`~02Limit:~~ 10`), and the price into `item_cost_value` as
`"~%02d%d"`, style 6 when the party's money is below the price and 0
otherwise. The price comes from `FUN_005a9b00` over the entry's handle (record
`+0x58`, through `FUN_005bfc20` and `FUN_005a9f90`), and is a formula per kind:
an item's own price through its definition, `(level / 10 + 1)^2 * 2000` for a
level advance (the factor capped at 5), and powers for the skill entries. The
game prices no entry it has not selected, so a reader has the selected
entry's price and nothing else. `inventory_count` is the gear count and limit
(`%d/%d`), and `money_value` holds the party's money.

Text escapes in these items: `~NN` selects a style (`~06` is the
cannot-afford red, `~02` the limit line, `~05` the footers), `~~` ends one, and
a `$` token that is not a `$MENU_` prompt is drawn as its name (`$HP` as `HP`,
seen in the retail shop's Health Pack description).

Money: no console command named for it is among the executable's command
strings (`set`, `setincrement`, `runscript`, `loadmap`, `openmenu`, ...); the
game-variable registry was not searched for a money entry. The script function `setInventoryCount` (`0x49ed40`, table `0x68b368`) sets it when
its first argument is `"MONEY"`, and the retail script
`scripts/act1/genosha/genosha1/temp_addmoney.py` adds 2000 through it, so the
console's `runscript act1/genosha/genosha1/temp_addmoney` gives money through
the game's own path.

Measured (Continue into `act2/jungle/jungle1`, `openmenu shop`): training tab
lit with 12 entries (four heroes by level advance, bonus skill point,
redistribute skills), 23 window rows of 8 units; a click on `buy` lit
`shop_option01` and the list became 13 entries (Energy Pack .. Grab Bag); a
click on Health Pack selected entry 1 and the game priced it at 100; with
2000 money from `temp_addmoney`, a click on the selected Health Pack bought one
(money 2000 -> 1900, `pot_health_value` 3 -> 4); `$MENU_OK Accept` closed the
shop to gameplay.

## The stash (`CMenuShop` with `menu+0x18e8` bit 0)

`openmenu stash` opens `CMenuShop` with bit 0 of `menu+0x18e8` set by the
XMLB's `stash="true"` (`FUN_005d39c0`). Its tabs are `stash_option01`
(`stash`) and `stash_option02` (`inventory`); `menu+0x18d8` is the open tab,
inventory (1) when it opens, and axis 0 cycles the two (`FUN_005d36a0`).
`CMenuShop::onMouse` tests the `stash_option` boxes in place of the shop's.
The list holds gear only: the inventory tab the party's unequipped gear, the
stash tab what is stored. The selection update (`FUN_005d30d0`) writes the
gear's description into `item_desc` and, on the inventory tab, the stash
count (`stash`, `%d/60`) into `inventory_count`, on the stash tab the gear
count (`gear`, `%d/20`); it writes no cost or money. On `MENU_ACCEPT` the
update (`FUN_005d3ba0`) moves the selected gear to the other side; its
per-tab paths through the inventory singleton `0x00480a00` are not read.
The footers are `desctext2` `$MENU_ACCEPT Store` and `desctext3` `$MENU_OK
Accept`, which closes it.

Measured (Continue into `act2/jungle/jungle1`): the party opened the stash
with no gear and stash 0/60. After buying a Fortified Waistband, the
inventory tab listed it; `MENU_ACCEPT` stored it (stash 1/60, empty list), the
stash tab listed it, and `MENU_ACCEPT` there took it back (gear 1/20).

## The codex (`CMenuCodex`) and its list (`CMenuItemListCodex`)

`CMenuCodex` (vtable `0x69e6d4`) is the hero codex `openmenu codex` opens. Its
init (`FUN_005b0f30`) loads `data/codex.xmlb` into the list; `FUN_005b0a60`
keeps the list at `menu+0x18dc`, shows mode 0's group and hides `desctext2`
(`$MENU_ACCEPT Select`), so the retail screen never offers Accept. Its items:
`model` (the turning hero), `title` `Codex`, `list`, `desc` (a text box with the
entry's description in the game's own `\n` lines), `name` (`~03` and the
hero's name), `up_arrow`/`down_arrow`, and the footers `desctext1`
`$MENU_BACK Back`, `desctext3` `$MENU_DETAILS Details` and `desctext5`
`$DPAD_DN Scroll`.

`menu+0x18d8` is the mode: 0 the list, 1 the description (`desc`, `name` and
the Scroll footer shown, the list hidden). The update (`FUN_005b1780`):

- when the published input carries `MENU_ACCEPT` (input manager vfunc `+0x160`,
  `FUN_005d4d60`: bit 4 of the bits vfunc `+0x140` returns) and the list
  exists, it calls the input manager's vfunc `+0xe8` with 6 and loads the
  selected entry: `FUN_005bfc70` takes the selected record's handle (record
  `+0x58`), and `FUN_005b0ba0` passes it to `FUN_005adcf0` for the item named
  at `0x69e674` and sets `menu+0x18e1`, so the next update runs
  `FUN_005b1340`. Selecting an entry alone loads nothing: `desc` and `name`
  keep the last accepted entry;
- when it carries `MENU_DETAILS` (bit 9, action 9), it flips the mode, hiding
  one group and showing the other (`FUN_005acf30`), and refreshes the list.

Its `onMouse` (`0x5b0c80`, a region Ghidra had not made a function): a
`WM_LBUTTONUP` on `desctext3` publishes action 9; button presses inside
`(0x146,0x3f)-(0x1e5,0x13f)`, the model's area, publish axis 2 at -1.0/+1.0
(turn the model); in mode 1 the wheel and clicks on the arrows publish axis 3
(scroll the description). Then it runs the base `CMenu::onMouse`. `Back`
closes the codex from either mode.

`CMenuItemListCodex` (vtable `0x6a0724`) differs from `CMenuItemListBox` only in
its parse (`+0x44`, `0x5c3b40`) and destructor: the entry getter, `onMouse`,
the stepping and every field above are the list box's, so a click on an
unselected entry walks the selection to it and a click on the selected entry
publishes `MENU_ACCEPT`, which is what loads it.

Measured (Continue into `act2/jungle/jungle1`, `openmenu codex`): 15 entries
(Magneto .. Scarlet Witch) in an 11-row window; one click on Bishop selected
it and Details showed Magneto, the entry loaded at open; a second click on the
selected Bishop and then Details showed Bishop. The menu pad's Down walked the
selection to Scarlet Witch (entry 14), moving the window's top to 4, and A then
Details showed her description. Wolverine's description is 21 lines.

## The world map (`CMenuWorldMap`)

`CMenuWorldMap` (vtable `0x69ece4`) is the extraction-point travel screen;
the script command `extractionPoint` (`0x004a6b50`) opens it with
`openmenu('worldmap')`, and the console's `openmenu worldmap` opens the same.
Its items: `title` `World Map`, the act tabs `option01_text..option05_text`
(`act 1`..`act 5`), `map_title` (a `CMenuItemListCycle` holding the open act's
region, `Genosha`, `Antarctica`, ...), `extract_description` (text box), the
point rows `list01_01..list01_07` (`list01_01` is a `CMenuItemListItems`
owning the seven; the rest are text items it fills), the `map` model and the
footers `desctext1` `$MENU_BACK Back`, `desctext2` `$MENU_BACK back` and
`desctext4` `$MENU_ACCEPT go`.

`menu+0x18d8` is the open act (1..5) and `menu+0x18dc` the point list. Init
(`FUN_005e8c40`) opens the party's current act (`FUN_0046dce0` vfunc `+0x274`)
through `FUN_005e8a70`, which enables each act tab whose act has an unlocked
point (`FUN_005e7d70`, `FUN_005ade70`), lights the open one and selects the
act's region in `map_title`. The list's refresh (`FUN_005c4620`) enables the
rows of unlocked points and focuses the first enabled one. The list's
selection is not a field: `FUN_005c45b0` returns the row that is the menu's
focused item (`menu+0x324`). Axis 0 steps the act (`FUN_005e8ff0` ->
`FUN_005e8d20`, skipping acts with nothing unlocked).

The update (`FUN_005e8d90`) first spends the queued steps `menu+0x18cc`
(acts, axis 0) and `menu+0x18d0` (points, axis 1), one per tick of the timer
at `menu+0x18d4`. On `MENU_ACCEPT` (`FUN_005d4d60`) it looks the focused
point up in the act (`FUN_00468530` vfunc `+8`); when it is unlocked (vfunc
`+0x1c`) and its map is not the current map (`_stricmp` against
`FUN_00484990` vfunc `+0x5c`), it writes `loadextraction <map> extract` to the
console buffer at `0x8b1380` and closes the menus (vfunc `+0x74`); otherwise
it refuses (input manager vfunc `+0xe8` with 7). Each update also writes the
focused point's description into `extract_description` (`FUN_005e7c90`).

Its `onMouse` (`FUN_005e7f10`) tests raw boxes (`top = [0x72] - [0x76] - 1`):

- on `WM_LBUTTONUP` over an enabled act tab other than the lit one, it
  publishes one axis 0 step toward it (-1.0 left, +1.0 right) and queues the
  rest at `menu+0x18cc`;
- on any message over an enabled point row other than the focused one, it
  publishes one axis 1 step toward it (+1.0 up) and queues the rest at
  `menu+0x18d0`: a pointer moving over the rows moves the selection;
- on `WM_LBUTTONUP` over the focused row (or an enabled row whose item flags
  carry `0x04` or `0x10`), it publishes `MENU_ACCEPT`, which travels;
- otherwise it runs the base `CMenu::onMouse`, where the footers' text items
  publish their prompt.

Which points are unlocked lives in `CExtractionPointSystem` (see
[extraction](extraction.md)); a map's load script unlocks its own point with
`extractionUnlock("")` (`act2/savage/savage1.py`), and the in-world pad's
`common/extraction/exp_activate.py` does the same.

Measured (Continue into `act2/jungle/jungle1`): `openmenu worldmap` opened on
act 2 with no act tab and no point enabled. After the console's
`loadmap act2/savage/savage1`, act 2 and `Avalon` were enabled, `Avalon`
focused; after `loadmap act1/sanctuary/sanctuary1`, the map opened on act 1
with `Sanctuary` focused and acts 1 and 2 enabled. A click on `act 2` opened
it with `Avalon` focused; go on the point the party stood on kept the world
map; A on `Avalon` closed it, loaded savage1 and put the party at its
extraction point, and the world map then reopened on act 2.

## The review (`CMenuReviewPaths`)

`CMenuReviewPaths` (vtable `0x69ee7c`) is the PDA's review screen; the
console's `openmenu review` opens it. Its items: `title` `Review`, the tabs
`option01_text..option05_text` (`screens`, `cinematics`, `comics`,
`concepts`, `stats`), their `option0N_focus` art, the `list`
(`CMenuItemListBox`, 27 rows of 8) and the footers `desctext2`
`$DPAD_UP scroll` (hidden), `desctext3` `$MENU_ACCEPT Select` (shown on every
tab but stats) and `desctext4` `$MENU_BACK back`.

Init (`FUN_005d1c60`) takes the open tab `DAT_008afef0` from the manager
(vfunc `+400`), enables and lights the tabs and fills the list: stats through
`FUN_005d0c20`, the others through `FUN_005d1220`. A stats record holds
`  <name>\t<n> of <m>` under `~02Act N~~` headers; the list draws the text
after the tab as a second column, so the model reads it as the entry's value
(the record's text runs to `+0x58`, where its handle starts). The other tabs
list one entry per unlockable, `[Locked Screen]` and the like when locked.

The update (`FUN_005d18a0`) spends the queued tab steps `menu+0x18cc`, one per
tick of the timer at `menu+0x18d4`; events 9..12 switch the tab
(`FUN_005d0890`, `FUN_005d1780`). On `MENU_ACCEPT`, when the list has entries
and the list's vfunc `+0x88` allows the selected one, it plays the accept
sound (input manager vfunc `+0xe8` with 6), stores the selection in
`DAT_008afef4[tab]` and shows the entry through `FUN_004ae7e0` vfunc
`+0x40`/`+0x54`; `Credits` opens the `credits` menu (`CMenuCredits`).
Otherwise it refuses (`+0xe8` with 7).

Its `onMouse` (`FUN_005d04d0`) on `WM_LBUTTONUP`/`WM_RBUTTONUP` over a shown
tab other than the lit one (raw box, `top = [0x72] - [0x76] - 1`) publishes
one axis 0 step toward it and queues the rest at `menu+0x18cc`; otherwise it
runs the base `CMenu::onMouse`, where the list box and the footers take the
click.

Measured (Continue into `act2/jungle/jungle1`, `openmenu review` from
gameplay): it opened on stats with 42 entries (`Comic Books` `0 of 3`, ...).
A click on `cinematics` opened it with `Credits` selected and the logos and
the promo unlocked; A on `Credits` opened the credits, and Esc returned to the
review. Screens held `Ultimate Bishop` and `Garokk` unlocked; comics and
concepts were all locked.

## The region list (`CMenuRegion`)

`CMenuRegion` (vtable `0x69f594`) is the online flow's region picker; the
console's `openmenu region` opens it (no `openmenu region` string is in the
executable or another menu's XMLB, so how retail play reaches it is not
established). Its items: `text_title` `Region Menu`, `data_name`, the list
`text_list` (`CMenuItemListBox`, 25 rows of 8) and the footers `desctext1`
`$MENU_BACK Back`, `desctext3` `$MENU_SUBTRACT Refresh` and `desctext5`
`$MENU_ACCEPT Select`.

Its parse hook (`FUN_005d01c0`) keeps the list at `menu+0x1960` and fills it
(`FUN_005d00b0`): one record `<name>\t<count>` per region the network layer
(`FUN_00606f40`) holds, each with its region handle. With no online service
that list is empty. The update (`FUN_005d02d0`) reads the input bits (input
manager vfunc `+0x140`): on `MENU_ACCEPT` (vfunc `+0x160`) it stores the
selected entry's handle (`FUN_005bfc70`, `FUN_006158f0`), plays the accept
sound and runs `openmenu campaign_lobby` through the console, whether or not
the list has entries; on bit 8 (Refresh) it clears the list and asks the
network layer again (`FUN_006158e0`). Its `onMouse` (`FUN_005caa80`) is the
online menus' shared one: it tests the lobby's text items, none of which the
region has, and otherwise runs the base `CMenu::onMouse`.

Measured (Continue into `act2/jungle/jungle1`, `openmenu region` from
gameplay): the list was empty; Enter opened `campaign_lobby`
(`CMenuCampaignLobby`, Host Game / Join Game); Back on a region opened over
the lobby returned to the lobby.

### Escape in a menu with a focused item

The retail keyboard binds Escape to two rows: Pause (row 17, in slot 0 of the
master table and every bank copy) and, in the menu banks, HighAttack as the
alternate Back (slot 2). One Escape press therefore sets action bits 19, 20 and
21 (and 5): Pause's two actions, `$MENU_BACK`, and accept's HighAttack.

Menu input reaches a menu through `FUN_005aaeb0` (the head of its update). With
no focused item it runs Back (menu vtable `+0x24`, `FUN_005ad7c0`: the item
flagged `menu_cancel`, else the close handler at `+0x2c`) and then, separately,
Start (`+0x20`). With a focused item it returns after the item's update
(`FUN_005bbfe0`, item vtable `+0x28`), which tests bit 20 first and only
without it looks at bit 21:

```
if (!(pressed >> 20 & 1)) { if (pressed >> 21 & 1) menu->+0x24(); }  // Back
else                                                  menu->+0x20();  // Start
```

Start is `FUN_005ad730` for every menu that does not override slot `+0x20`: it
activates the item flagged `menu_ok` and does nothing when none exists. The
PDA (`FUN_005cc630`) and the main-menu Options override it (Escape closes the
PDA and opens Advanced Options); the shop closed under Escape in a measured run
without the mechanism being read. The world map and the codex do not override
it: measured with a console-opened menu, Escape left both open before the
override and closes both after it, because a focused row ran an empty Start and
the Back was never reached. A
pad's B (bit 21 alone) and a click on the footer (`FUN_005e7f10` publishing the
prompt) never had the problem.

`src/native/menu_start_override.cpp` overrides `FUN_005ad730`: when the base
Start did nothing, the dispatch is the focused item's (return address
`0x005bc08a`), and the same press carried bit 21, it runs the menu's Back. That
is a deliberate difference from retail; the unfocused path and menus with their
own Start are untouched. Measured by `tools/live_case.py extract-keyboard-back`
(world map opened through the real pad and the Xtract choice; Escape closes it
and the party walks again) and `options-back` (Escape still opens Advanced
Options).

## What is NOT established

- **No per-item text measurement.** The hit box comes from the item's scene
  bounding box (`FUN_005bc4b0`), not from the glyph run the label draws. A row
  whose art overhangs its text is therefore reachable over the art. Measured
  only as the box the game itself tests, which is the only box that decides
  anything.
- **`<item>` count versus pressable rows.** `menu+0x1608` counts every `<item>`
  element, so a reader must still apply `item+0x54 & 8` (enabled),
  `item+0x24` (a non-empty `usecmd`) and the `neverfocus` rows before offering
  anything. `main` is 42 items of which 6 are the label rows; `team` is 142 and
  `pda` 111, so a menu is not a short list of buttons.
- **`CMenuMgr+0x204` is not named.** It returns whether any menu is up; it is
  the branch at `FUN_005f9eb0`, not a member this document has read. The reader
  does not use it -- it reads the active menu field directly -- so nothing
  depends on it.
- **The `roster_*` rows of `team` are marked `neverfocus` in the shipped
  XMLB.** Their boxes exist and are hit-testable, but nothing in the game's own
  item walk offers them, so they are correctly not targets for a contact
  either. The world map's `list01_*` rows are offered once the list's refresh
  enables them (`FUN_005c4620`).
- **The world map's pad walk between points.** Only one point per act was
  unlocked in the measured run, so the menu pad's Up/Down between two enabled
  points is unit-tested only. `CMenuItemListItems`'s own stepping (vfunc
  `+0x58`, `0x5c4090`) and its `+0x2a4` flags are not read, nor are
  `FUN_005e7d70`, `FUN_00468530`'s point object beyond `+0xc`, `+0x10`, `+0x1c`,
  and the region map's marker (`FUN_005e8590`).
- **Orphan regions.** A previous pass found ~161 disassembled regions in
  `XMen2.exe` that Ghidra never made into functions (`FUN_005d83d0`,
  `FUN_005ae0a0`'s neighbours and `FUN_005d6220` are three of them, recovered by
  disassembly here). Whole-image coverage is therefore incomplete, and a menu
  behaviour living entirely inside one of those regions would not have been
  found by this document.
- **Item flag `0x10`.** Navigation skips it and list code clears it at
  `0x005c0968`; what sets it is not found.
- **`item+0x57` mode bits** and **`item+0x56` filter** are parsed, not
  understood.
- **The writer of the pool-2 generation `pool[0]`.**
- **The other list subclasses' entries, popup text and buttons, and a bar's
  value text** are not read. `CMenuItemListBox` and `CMenuItemListCodex`
  entries are; a subclass that overrides the entry getter (`+0xe4`) is not
  assumed to share the record table.
- **The codex's description scroll and model turn** (axes 3 and 2) are not
  driven by the touch menu; it shows the whole description itself and no
  model. `FUN_005b1340`, `FUN_005adcf0` and the manager's vfunc `+0xe8` are
  not read.
- **A shop list longer than its window.** The jungle shop's lists hold at most
  13 entries against 23 rows, so walking the selection to an entry outside the
  window is unit-tested only.
- **The price of an unselected shop entry.** The game computes it only on
  selection (`FUN_005a9b00`); the touch menu shows the selected entry's price
  and does not compute the others.
- **`menu+0x18e8` beyond bit 0.** It read 0 when the shop opened and 2 after a
  tab change and a purchase; only bit 0 (the stash) is used.
- **The command-less main rows by touch** (`danger room`, `play online`) are
  reached by the pad walk and A; only the keyboard path has been measured.
- **The `~NN` text escape's table.** `~06` is the shop's cannot-afford red;
  the other styles are not read. The touch menu strips the escapes.
- **No title item on the PDA or the team menu.** Neither has a `label_<menu>`
  or `title*` item, so their touch menus have no header band.
- **Navigation of a menu whose links are all 0** (`pause`): the model offers
  only the anchor row.
- **The anchor fallback** uses slot order where the game's default focus walks
  the name tree.
