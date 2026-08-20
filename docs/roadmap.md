# Roadmap

What this port is trying to become, beyond running. The three features in
[`README.md`](../README.md) are the ones that have *landed*; this file is the
stated direction, with honest status against each.

Status words mean what they mean elsewhere in this repo: **shipped** is
re-verified against the real game on real data, **partial** works but has a
named gap, **not started** has no code. An internal trace ("the call site was
reached") is a mechanism check, never faithfulness.

## 1. Performance

**Partial.** The port must be playable, not merely correct.

Landed: slow frames are attributed to guest or host at the moment each ends;
`X2_PROFILE` is a sampling profiler that histograms the current guest body, so
a body that runs a lot is sampled a lot (C211 — it names hotspots the fixed
hash table of the `hotep` probe refuses); the movie decoder's rendezvous spin
was replaced with a bounded wait, measured A/B.

Open: no frame-time budget is defined, so "fast enough" has no number yet.
Measure with `tools/x2ctl.py status`, which reports avg/min/max frame time over
the run's intervals.

## 2. Faster loading

**Partial, and the big win is in.** The level-load stall was the runtime's
LINEAR dispatch-table lookup, not the guest building geometry: `find()` scanned
each module's ~6000-entry table on every dispatch. Binary search over a sorted
table cuts the load frame from 4592 ms to ~500 ms — **9.2x** — with identical
dispatch resolution (C210).

Open: a ~500 ms load frame is still a visible hitch, and asset I/O has not been
profiled at all.

## 3. Xbox button prompts as SVG glyphs

**Partial** — this is feature 3 in the README. This port's own SVGs are
published into unused bytes of the PC font and returned at the game's RE'd
physical-input naming boundary, for SDL-classified Xbox controllers only. No
Xbox asset is shipped; the art is this port's.

**The gate is met.** The main menu prompt bar draws `B BACK` / `A SELECT`
with the real button art — upright, on the text baseline and without the
game's square brackets, all three of which were wrong in the first capture and
none of which that capture could show, and the tutorial dialog draws the button in place of
`[ENTER] CONTINUE...` — captured in game, on the shipping codepoints
(`scratch/shots/ship.png`, C219). With a pad connected the prompt follows the
pad, because the label override answers with the row's pad binding.

What blocked it for so long was this port's own bug, not the game: the font
builder published glyph metrics in normalised units where XMen2.exe wants
pixels, so every glyph drew at nine tenths of a pixel — invisible, and
indistinguishable from a renderer refusing the codepoint (C220). The builder now
refuses metrics whose units do not match the font's own.

Open: three of the four fonts the game loads (`X2F_big`, `X2F_hud_PC`,
`font_XMEN_digital`) are pixel format 15, and the builder refuses to re-encode
them, so a prompt drawn in one of those would still be blank. Whether any is.

**The d-pad and the shoulder buttons were unreadable** (#88, #89): all four
d-pad directions drew one icon, and RB and RT were the same letter on two
rectangles that differed only in fill. Both fixed, and the art now lives in
`shared/port-assets` -- consumed, not vendored, because the next port was about
to redraw the same controller. The bar for accepting a glyph is a rasterised
sheet at the shipping cell size over light, dark and mid-tone backgrounds;
four earlier attempts died there.

**And the gate covers the MENU, not gameplay.** Reported from a real play
session 2026-08-19 (issue #87): in gameplay the prompts still name keyboard
keys with a pad connected. The captures above stand -- they were the main menu
bar and the conversation dialog -- but a gameplay HUD prompt goes through
neither, and the caller census the feature doc asks for was never finished. So
this feature is met on two screens and unmeasured everywhere else.

## 4. Input hotswap

**Shipped for the synthetic pad; unverified on real hardware.** Enumeration was
always real -- SDL3 plus the game's own DirectInput enumeration and connection
callbacks, with late attach and detach. What was missing was a press, and it
was missing at two independent layers, both now fixed and both re-checkable
live with `tools/x2ctl.py input`.

**The host half (ad78ca9).** SDL discards joystick BUTTON state when no window
holds keyboard focus while writing AXIS state through regardless, so the pad
enumerated, its sticks moved, and every button read released forever.
`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` before the gamepad subsystem
starts. The window belongs to the guest -- a 2005 game creating it through a
Win32 layer SDL only backs -- so SDL's notion of focus is not something this
port can gate input on.

**The game half (fcedf76).** With the button arriving, pad A still did nothing.
XMen2.exe keeps sixteen binding sets in four banks; the game evaluates only the
working copies 4..7, which `FUN_0061b030` fills from the masters 0..3 *before*
the port's preset runs, and it then overwrites slots 2 and 3 of those copies
with its own menu keys -- row 4 slot 2 is Return, the `[ENTER]` on a dialog
prompt. The preset was going into the master's slot 2: present, correct, and
never read. It now goes into slot 1 -- the alternate the game itself leaves
free, persisted as `Controls\Player%d\<row>2` -- and is published to sets 0,
4 and 12 the way the game publishes it (C215).

Measured after: pad A sets player 0's `physical[4]` to 1.000 exactly as Return
does, three presses carry the tutorial conversation to its end, and
`leftx=-1` sets `physical[0]` to -1.000.

Open: **no real controller has been attached to this machine**, so every
reading above comes from the synthetic pad. That is a real gap and it is why
this is not called verified. The `x2ctl.py input` probe is what a hardware run
should be checked with.

**The pad's TRIGGERS were delivering nothing at all** (C222, #85): both 360
triggers share one DirectInput axis and the port halved it, so a fully squeezed
trigger reached half the range the game asked for -- and on the synthetic pad a
fresh axis presented BOTH triggers half held, cancelling to centre. `Power` is
bound to the right trigger, so the modifier every power needs never engaged.
Fixed and measured: code 0x06 resolves to 1.000 and reaches the game's own
player input array. Neither the unit test nor the probe could have caught it --
one checked the sign, the other printed a down/up bit.

**The two gameplay reports now have live evidence** (#85, #86). RT+A drives
both physical inputs at full scale and casts the selected hero power in an
initialized boot-map run (C224). Healing was a preset omission: the retained
PC `TargetLock` row is the control named by the shipped PS2 potion tutorial,
and Xbox Black is labelled **Use Health Pack**. The fixed preset maps that row
to modern RB; the live probe reports 22 pad rows and RB drives the assigned
player's action slot to `+1.000` (C227). A physical-controller capture remains
open because this machine only has the synthetic pad.

## 5. RmlUi for player mapping and input bindings

**Partial; the working UI and runtime path are in.** F1 opens a shipped RmlUi
overlay rendered through the same SDL_GPU command buffer as the game. Its
Dusklight-style tabs expose resolution plus windowed, borderless and exclusive
fullscreen presentation, and one page per player. The dependency is pinned by
commit and archive hash rather than following an unbounded branch.

Input policy is explicit. Four reusable keyboard profiles each own all 42 game
actions; a player assigned to Keyboard references one profile, so two players
can share the physical keyboard with different layouts. A controller is
assigned by persistent physical identity and uses the canonical Xbox/PS2
defaults — controller remapping is deliberately not a second settings system.
Explicit pad ownership is resolved before Auto selection, and one pad can feed
at most one player.

The live end-to-end check configured Player 2 as Keyboard 2, rebound Forward to
`I`, and persisted `input.profile1.row0=23`. Pure tests cover transactional
config round-trip and profile reuse across player numbers; a virtual-pad test
covers identical-controller live identities. The modal overlay suppresses
guest DirectInput state while open.

Open: real-pad hotplug/identity and fullscreen transitions need hardware/user
validation; controller navigation currently uses focus traversal rather than
Dusklight-style spatial navigation; the overlay is opened with F1 rather than
being wired into the original Options menu; keyboard prompt keycaps remain
issue #91.

**Decided 2026-08-18: the port keeps the PC base, and RmlUi takes over the
options system rather than sitting beside it.** Basing the port on the Xbox
release was considered and rejected for this purpose. The reason given was that
the PC option system does not fit the intended UI -- but the Xbox release is
fixed-output console hardware with essentially no resolution system to expose,
so it serves that goal less well, not better. And an options UI is exactly what
this architecture makes replaceable: the same category of work as the
controller defaults UI, and independent of which binary is underneath.

Switching base would also strand the part that is finished. The x86-32
translator, the Alchemy engine layer and the RE harness are already shared
repos and would carry over unchanged; what would NOT carry over is the ~26,000
lines of PC host layer -- the SDL3 GPU renderer and D3D8 host, the Win32-on-SDL3
layer, the PE loader, DirectSound and DirectInput -- because the Xbox release
needs an XBE loader, NV2A, and Xbox kernel APIs instead. The Wine oracle, which
every rendering question is currently settled against, would go too.

That is a decision about THIS port's UI, not a claim that an Xbox port is not
worth doing. It shares the lifter, so it stays cheap to start later.

Read [`docs/prior-art.md`](prior-art.md) **first** -- Dusklight is a shipping
CC0 port of the same shape that has already solved input binding and UI, and
whatever is taken from it gets cited in the file that takes it.

## 6. Start in the game, skipping the menus

**Verified as a development start path; player-facing policy is still open.**
`X2_BOOT_MAP=<map>` skips the intro movies, menus, difficulty dialog and story
movie, but now preserves the retail New Game initializer. It calls BehavEd's
`startFirstMission` (FUN_004a7b10), which installs the real default party, and
replaces only its subsequent `menus/new_game` script with the requested map.
The mechanism and RE evidence are in [`docs/RE/boot.md`](RE/boot.md).

Issue #83 was the regression that forced the boundary: the old bare
`loadmap ... 0 0` kept a nonexistent team, producing 0 of 5 resolved hero
handles and suppressing the tutorial's second conversation. The fixed path
measured 1 of 5 handles resolving and the second conversation entering
speaking/visible state (`0x18 -> 0x13`; C223).

Open for a player-facing “Start Game” option: save-slot selection and an
explicit difficulty policy. Party initialization is no longer skipped.

## How this work is done

These are the project's own rules, and each exists because its absence produced
a logged defect. The full set is in [`AGENTS.md`](../AGENTS.md).

- **RE first, then port.** A native override must reproduce the original's
  return value, not merely its stack effect — check the CALL SITE, not the
  decompiler's signature.
- **It must read like a game's source, not like output.** Code is organized by
  subsystem ownership, in the file a native game would put it in. A new
  subsystem gets its own file.
- **Workflow, tooling and rules come before features.** If consulting the
  registries is hard, that is a defect that outranks the task in hand.
- **Shell is `run.sh` and nothing else.** Everything else is Python; shell
  scripts are not maintainable at this size.
- **A play-through observes; it never gates.** Drive a live run through
  `--control` / `tools/x2ctl.py` instead of scheduling presses in advance.
