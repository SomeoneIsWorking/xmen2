# The conversation system

Reverse-engineered from the 2005 PC build of **X-Men Legends II: Rise of
Apocalypse** (`XMen2.exe`, image base `0x00400000`). Every address below is that
image's preferred base; every field offset was read from the code that writes
it, and the runtime values quoted were captured from a live run (see
*Provenance* at the end).

This subsystem drives every scripted conversation in the game: the intro
exchanges, the mission briefings, the NPC dialogue trees. It is also the hook by
which conversations start scripts — which is how the tutorial spawns the
playable hero — so it sits on the critical path of the first level.

Written up because it is not documented anywhere else, and a port is not the
only reason someone might want it: the same structures drive modding
(conversation editing, adding responses that launch scripts) and translation
(the `%TOKEN%` substitution below).

---

## 1. The assets

```
Conversations/<act>/<level>/<name>.engb      binary XMLB, English
Conversations/<act>/<level>/<name>.XMLB      binary XMLB, the localised set
Scripts/<act>/<level>/<name>.py              plain-text BehavEd script
```

`.engb` and `.XMLB` are the same binary-XML container (Alchemy's XMLB). The
schema, as it appears decoded:

```xml
<conversation>
  <startCondition noreturntogamecamatend="true" runonce="true">
    <participant name="default">
      <line soundtoplay="voice/cyclops/1_intro_020_010"
            text="%CYCLOPS%: Nightcrawler, we've located the Professor.">
        <response text="%BLANK%">
          <line soundtoplay="voice/nightcrawler/1_intro_1"
                text="%NIGHTCRAWLER%: Will do.">
            <response chosenscriptfile="act0/tutorial/tutorial1/nightcrawler_spawn"
                      conversationend="true"
                      text="%BLANK%"/>
```

Lines and responses **alternate and nest**. A `<line>` holds what a character
says; each `<response>` under it is a choice the player can pick, and each
response holds the `<line>` that follows it. `%BLANK%` as a response text means
"no visible choice" — the conversation simply advances on the accept button.

`%CYCLOPS%`, `%NIGHTCRAWLER%`, `%PLAYER%` and friends are substitution tokens
resolved at parse time; `%PLAYER%` in particular is special-cased in the parser
(compared against the constant at `0x00685d18`).

### Response attributes

| attribute | what it does |
|---|---|
| `text` / `textb` | the choice's label; `%BLANK%` means "no choice, just advance" |
| `chosenscriptfile` | a script launched **when this response is chosen** |
| `scriptfile` | a script launched when the response's record is walked |
| `scriptcommand` | ditto, launched before `scriptfile` |
| `conditionscript` | launched before the child lines are filtered for eligibility |
| `conversationend` | `"true"` ends the conversation after this response |
| `tagjump` / `tagIndex` | jump to a labelled line instead of a child |
| `actorAnimation` | animation to play on the speaking actor |

The attribute-name constants live at `0x00685efc` (`conversationEnd`),
`0x00685f38` (`chosenScriptFile`), `0x00685f2c` (`scriptFile`), `0x00685f1c`
(`scriptCommand`), `0x00685f0c` (`conditionScript`), `0x00685f6c` (`tagjump`),
`0x00685ef0` (`tagIndex`), `0x00685f4c` (`actorAnimation`), `0x00685cbc`
(`text`), `0x00685f5c` (`textb`), `0x00685cb0` (`response`), `0x00685ca8`
(`line`), `0x00685f64` (`%BLANK%`), `0x00681968` (the empty string).

---

## 2. The record layout

`FUN_00458820` parses one line-or-response node into a record. **Every string
attribute is stored as a pointer/length PAIR, and the pointer is the FIRST
dword.** The parser is handed `record + 4`, so its own `+0x18` is the record's
`+0x1c` — which is the single easiest thing to get wrong here, and does not
announce itself: read `+0x1c` as "the second half of the `+0x18` pair" and a
real script path looks like a length.

| offset | field |
|---|---|
| `+0x08` / `+0x0c` | `text` (char *, length) |
| `+0x14` / `+0x18` | `scriptFile` |
| `+0x1c` / `+0x20` | `chosenScriptFile` |
| `+0x24` / `+0x28` | `scriptCommand` |
| `+0x2c` / `+0x30` | `conditionScript` |
| `+0x34` | the tag string (compared for `tagjump`) |
| `+0x3c` | `tagjump` target |
| `+0x6c` | a second string compared during a tag jump |
| `+0x70` / `+0x74` | `actorAnimation` |
| `+0x7c` | flags — **bit `0x2` = `conversationEnd`** |
| `+0x84` | tag index |
| `+0x8c[]` | child record **ids** (not pointers) |
| `+0xac` | how many children |
| `+0x109` | a byte the update tests on the speaking actor's record |

Records are addressed by **id**, not by pointer: `FUN_00457460(id)` and
`FUN_004573f0(id)` map an id to a record.

`FUN_00458820` returns `AL = 1` for a fully parsed node and `AL = 0` for the
short path (a text-only node, or one whose text is `%BLANK%`/`%PLAYER%`). Its
caller `FUN_00458d10` walks the tree, appending each child line to its parent's
`+0x8c` array and recursing at `0x00459802`. Note that `FUN_00458d10` contains
a *second* copy of the attribute parsing (at `0x00458fdc` and `0x00459709`)
which is only reached on the `AL = 0` path — that copy stores
`chosenScriptFile` at `+0x1c`/`+0x20` of a record it holds in `EDI`, which is
the same field, reached by a different route.

---

## 3. The manager

One singleton, `0x00717aac`, constructed on demand by `FUN_00458380`
(`FUN_004583f0` is the non-lazy accessor). The object is about `0x23a000` bytes.

| offset | field |
|---|---|
| `+0x004` | the record pool base (`this + 4` is passed as `this` to the pool helpers) |
| `+0x4bc` | the current line's id |
| `+0x4c0[]` | the offered responses' ids; `0xFFFFFFFF` is an empty slot |
| `+0x4e0` | how many slots are offered |
| `+0x21b18/1c/20` | audio handles for the line being spoken |
| `+0x21b24` | **flags**: `0x1` speaking · `0x2` visible · `0x8` ending · `0x10` subsystem enabled |
| `+0x21b26` | tag index (int16) — also the highlighted response |
| `+0x21b28` | tag count (int16) |
| `+0x21b2a` | highlight wrap counter (int16, clamped to 0..2) |
| `+0x21b30` | actor A, bound into the script manager on launch |
| `+0x21b34` | actor B, bound only while `FUN_004654d0` says it is still live |
| `+0x21b38` | draw list A (the subtitle/prompt surface) |
| `+0x21b3c` | draw list B |
| `+0x21b5c` | float: no advance is accepted before this time (the debounce) |
| `+0x21b80` | the currently playing voice handle |
| `+0x239a0` | the response id chosen this frame; `0` for none |
| `+0x239a4` | accept-state bits |
| `+0x239b0` | the prompt's layout array |
| `+0x239c0` | a 32-bit eligibility bitmap over a record's children |

### The vtable

Base **`0x00685e04`** (the dword at `0x00685e00` is the MSVC RTTI pointer, which
sits at vtable−4; the class name string nearby reads `conversations\...`).

| slot | function | what it is |
|---|---|---|
| `+0x00` | `FUN_00458120` | |
| `+0x04` | `FUN_00455af0` | reset / begin |
| `+0x08` | `FUN_00455d90` | tear down (hides, clears the speaking bit) |
| `+0x0c` | `FUN_0045d1a0` | **the per-frame update** |
| `+0x10` | `FUN_00459860` | |
| `+0x14` | `FUN_0045c950` | starts a conversation (calls `setVisible(true)`) |
| `+0x18` | `FUN_0045d5d0` | **`chooseResponse(n)`** |
| `+0x1c` | `FUN_004553d0` | |
| `+0x20` | `FUN_00458010` | **`isVisible()`** — `(flags >> 1) & 1`, in `AL` |
| `+0x28` | `FUN_00458030` | |
| `+0x34` | `FUN_0045b610` | |
| `+0x48` | `FUN_00458000` | returns `this + 0x21b44` |

`FUN_00458020` (`(flags >> 2) & 1`) is the speaking predicate.

---

## 4. The state machine

```
FUN_0045d1a0   update()          called once per frame by the level state machine
 ├ flags & 0x10 ................ subsystem enabled, else return
 ├ reset both draw lists ....... FUN_005ef1a0 on +0x21b38 and +0x21b3c
 ├ vt[0x20] isVisible() ........ else return
 ├ FUN_00456440 -> slot ........ 0x3fffffff means the conversation is over
 ├ +0x239a0 != 0 ............... a response was chosen: applyResponse and return
 ├ flags & 0x8 ................. ending: FUN_004585f0 and return
 ├ FUN_004573f0(+0x4bc) ........ the current line; NULL -> setVisible(false), return
 ├ THE ACCEPT GATE  (below)
 ├ the highlight, moved by the stick / d-pad
 ├ the "$MENU_ACCEPT" prompt, drawn while flags & 0x2
 └ four pad slots cleared for the next frame
```

### The accept gate

```c
input = FUN_005d8920();                     /* the input singleton */
if (input->vt[0x138](4)) {                  /* action 4 = accept */
    now = FUN_0046dce0()->vt[0x160]();      /* seconds, in ST(0) */
    if (now > this->[0x21b5c]) {            /* FCOMP; TEST AH,0x41 skips on <= */
        FUN_005d8920()->vt[0xe8](6);        /* the accept sound */
        if (this->[0x21b80] != *(uint32 *)0x0069d05c) {
            FUN_00592480()->vt[0x74](this->[0x21b80]);   /* stop the voice */
            this->[0x239a4] &= 0xfe;
            this->[0x21b80] = *(uint32 *)0x0069d05c;
        }
        this->vt[0x18]((int16)this->[0x21b26]);          /* chooseResponse */
    }
}
```

Action index **4** is the accept button. On the keyboard that is Return —
verified by injection: the predicate flips to 1 on exactly the frame a Return is
injected and back to 0 on release.

### Choosing a response

```c
void chooseResponse(int sel)                 /* FUN_0045d5d0, __thiscall, ret 4 */
{
    int count = this->[0x4e0];
    if (sel < count && count > 0) {
        for (slot = 0, filled = 0; slot < this->[0x4e0]; slot++) {
            id = this->[0x4c0 + slot*4];
            if (id == 0xFFFFFFFF) continue;          /* a hole; sel does not count it */
            if (filled == sel && !FUN_00458700(this, id)) {   /* TRUE means DECLINED */
                line = FUN_004573f0(this, this->[0x4bc]);
                if (line) {
                    tag = (int16)this->[0x21b26] + 1;
                    line->[0x84] = (tag >= (int16)this->[0x21b28]) ? 0 : tag;
                }
                FUN_0045cde0(this, id);              /* applyResponse */
            }
            filled++;
        }
    }
    FUN_00458410(this);        /* reached by BOTH early exits too */
}
```

### Applying a response

`FUN_0045cde0` is where a conversation moves, ends, or launches its script.

```
FUN_0045cde0   applyResponse(id)
 ├ this->[0x239a0] = 0
 ├ rec = FUN_00457460(id); NULL -> return
 ├ if rec->[0x3c] (tagjump) is not "" -> search the line table for the tag
 │     match found -> the tagjump branch at 0x0045cfa7 (launches rec->[0x1c] too)
 ├ next = FUN_0045b6d0(rec)              /* the next line's id, or 0 */
 ├ this->[0x21b26] = 0 ; this->[0x4e0] = 0
 ├ next == 0  -> flags |= 0x8 (ending), stop the voice, setVisible(false),
 │               then LAUNCH rec->[0x1c] (chosenScriptFile) if non-empty
 ├ rec->[0x7c] & 0x2 (conversationEnd) -> same ending path, launch at 0x0045cf98
 └ otherwise  -> LAUNCH rec->[0x1c] at 0x0045d0dd, then advance to `next`:
                 mark its responses, FUN_0045b920, and set the tag index
```

So **`chosenScriptFile` is launched on all three exits**, and an empty string is
the normal case — the one-byte `REPE CMPSB` against `""` asks only whether the
first byte is NUL.

### Finding the next line

```c
uint32 nextLine(record *r)                   /* FUN_0045b6d0, __thiscall */
{
    if (!r->[0xac]) return 0;                /* no children */
    conv->[0x239c0] = 0;
    FUN_004559e0(&conv->[0x239c0]);          /* *p = ~*p, then clear bits 6..31 -> 0x3F */
    if (r->[0x2c][0]) launchScript(r->[0x2c], 1);        /* conditionScript */
    for (i = 0; i < r->[0xac]; i++)
        if (conv->[0x239c0 + (i>>5)*4] & (1u << (i & 31))) break;
    if (i == r->[0xac]) return 0;            /* nothing eligible */
    FUN_0045a100(r);                         /* scriptCommand, then scriptFile */
    return r->[0x8c + i*4];
}
```

The eligibility bitmap starts with the low six bits set, so up to six child
lines are candidates; a `conditionScript` is what clears bits to steer the
branch. Returning `0` is what ends a conversation.

---

## 5. Launching a script

```c
bool launchScript(const char *name, int flag)    /* FUN_00455600, __thiscall, ret 8 */
{
    sm = FUN_004a1670();                          /* the script manager singleton */
    sm->vt[0x14](this->[0x21b30] ? this->[0x21b30]->[0x1c] : 0);   /* actor A */
    sm = FUN_004a1670();
    sm->vt[0x18]( (this->[0x21b34] && FUN_004654d0(&this->[0x21b34]))
                  ? this->[0x21b34] : 0 );                          /* actor B */
    sm = FUN_004a1670();
    ok = sm->vt[0x3c](name, flag);                /* vt[0x3c] IS FUN_004a1320 */
    FUN_004a1670()->vt[0x1c]();                   /* clear actor A */
    FUN_004a1670()->vt[0x20]();                   /* clear actor B */
    return ok;                                    /* in AL */
}
```

`FUN_004a1320` is the by-name launcher: it builds `scripts/%s.py` and runs it.
It has **zero direct call sites** in the image — every caller reaches it through
vtable slot `+0x3c` of the script manager — which is worth knowing before
concluding from a cross-reference search that a launcher is dead code.

The other four launch sites in the exe are `0x004673cc` (`FUN_00467380`),
`0x0048a779` (`FUN_0048a5a0`), `0x0049fea6` (`FUN_0049fe70`) and `0x005f23b4`
(`FUN_005f2350`) — map entities and the level-entry script.

---

## 6. The "$MENU_ACCEPT" prompt

`0x0045d4c2`–`0x0045d55b` draws the accept-button prompt. It cannot be read off
a disassembly listing, because MSVC leaves floats on the x87 stack across
intervening integer pushes; the reconstruction below came from capturing a live
run instruction by instruction.

```c
conv   = FUN_004583f0();
target = conv->[0x21b38];
py     = (float *)(*(fn *)0x0067f9c4)(conv + 0x239b0, 2);   /* 91.6f */
px     = (float *)(*(fn *)0x0067f9c4)(conv + 0x239b0, 0);   /* 57.0f */
quad   = (*(fn *)0x0067f9ac)(&tmp, 1.0f, 1.0f, 1.0f, 1.0f);
colour = FUN_0040be00(quad);                    /* packs float RGBA -> 0xFFFFFFFF */
FUN_005ef270(target + 0x43c,
             __ftol(*px - 36.0f),               /* 21  */
             __ftol(*py + 16.0f),               /* 107 */
             0x20, 0x20,                        /* a 32x32 icon */
             1.0f, 10, colour, "$MENU_ACCEPT");
```

Three things the listing hides:

* **`0x0067217c` is `__ftol`.** It takes its argument on the **x87 stack** and
  consumes nothing from the integer stack. That is why the five `PUSH`es
  surrounding it are not its arguments — they belong to the `FUN_005ef270` call
  after it, and one `ADD ESP,0x24` cleans all nine dwords at the end.
* `[0x0067f9ac]` is `Gap::Math::igQuaternionf::igQuaternionf`. It consumes four
  float arguments with `RET 0x10`; `$MENU_ACCEPT` was pushed before those four,
  remains below them through the constructor and the caller-cleaned pack call,
  and becomes the final argument of `FUN_005ef270`. It is not a fifth
  constructor argument.
* `0x0040be00` packs a float RGBA quaternion into ARGB.
* `[0x0067f9c4]` indexes a layout array and returns a `float *`.

The constants are `[0x006819fc] = 16.0f` and `[0x00684b9c] = 36.0f`.

---

## 7. Game-over reasons

A `(name, id)` table at `0x006d9278`, read by `FUN_004a7220` — this is what the
BehavEd `gameover` command's argument is looked up in:

| id | token |
|---|---|
| 0 | `GAMEOVER_DEFAULT` |
| 1 | `GAMEOVER_BYSTANDER_DIED` |
| 2 | `GAMEOVER_GENERATOR` |
| 3 | `GAMEOVER_PERIMETER` |
| 4 | `GAMEOVER_CHARLIE` |
| 5 | `GAMEOVER_ABYSS` |

---

## 8. Worked example: the tutorial's opening

The scripts are plain text and ship with the game, so this whole chain is
readable without a disassembler once you know what drives it.

`Scripts/act0/tutorial/tutorial1/tutorial1.py` — the level-entry script:

```
remove ( "oz_explosion", "oz_explosion" )
setRecallActive("FALSE" )
...
lockControls(-1.000 )              # controls locked INDEFINITELY
setallaiactive("FALSE" )           # all AI off
cameraFocusToEntity("cam_prof", 128.000, 30.000, 0.000, 0.000 )
cameraFade(0.000, 1.000 )
waittimed ( 1.000 )
startConversation("act0/tutorial/tutorial1/1_introlevel_0020" )
```

`1_introlevel_0020` is two lines deep, and its **final** response carries
`chosenscriptfile="act0/tutorial/tutorial1/nightcrawler_spawn"` with
`conversationend="true"`. That script is:

```
waittimed ( 1.000 )
act("spwnr_nightcrawler", "spwnr_nightcrawler" )
```

— it triggers the spawner that puts the **playable hero in the level**. So the
player character does not exist until the opening conversation is advanced
twice.

`1_introlevel_0020b` follows, three responses deep, ending in
`conv_0020b_end`:

```
playanim (  "EA_ZONE5", "simplecyclops", "NONE", "" )
waittimed ( 1.000 )
cameraFade(1.000, 0.500 )
waittimed ( 0.500 )
remove ( "simplecyclops", "simplecyclops" )
cameraReset( )
cameraFade(0.000, 1.000 )
waittimed ( 1.000 )
lockControls(0.100 )               # controls handed back
setallaiactive("TRUE" )            # AI on
```

The invariant worth spelling out, because it explains a whole class of
symptoms: **between `tutorial1.py` and `conv_0020b_end`, the player has no
character and no control, and the AI is off.** Anything that prevents those two
conversations from completing leaves the level in that state permanently.

---

## Provenance

Static reading is from Ghidra exports of the retail `XMen2.exe`
(`tools/ghidra_export.sh`, `tools/recomp.py`). Runtime values — the record
addresses, the parsed `chosenScriptFile` lengths, the accept-button index, the
`__ftol` behaviour and the prompt's arguments — were captured from a live run
with the region recorder (`recomp.py emit --record LO-HI`, see
`src/native/x86_record.c`), 24,942 instructions over 221 passes of the update
and 123 over three passes of the prompt block.

The port of this subsystem is `src/native/conversation.c`; the recompiled
originals stay linked beside it as `__real_*`, so the two can be diffed. Where
this document and that file disagree, the file is the one that runs.
