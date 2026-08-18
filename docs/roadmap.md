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

Open: delivery and the 21-row bindable Xbox preset are implemented, but the
gate is an **in-game prompt capture** — a screenshot showing a prompt actually
drawn in the game, not a trace saying the boundary was reached. The control
channel (`tools/x2ctl.py shot`) is how to take it.

## 4. Input hotswap

**Partial, and it was wrongly recorded as shipped until 2026-08-18.**
Enumeration is real: SDL3 plus the game's own DirectInput enumeration and
connection callbacks, with late attach and detach exercised by a synthetic pad.

What was NOT tested is a press. SDL's virtual joystick reads zero on every
button until something sets one, and nothing set one — so the synthetic pad
proved the game FINDS a controller and proved nothing about input reaching it.
Reported as "the Xbox controller does nothing", and reproducible with no
hardware: `x2ctl.py pad a` leaves a conversation where it is while `key Return`
advances it.

One cause is fixed — the gamepad path never refreshed SDL's latched state,
where the keyboard and mouse paths always had. Buttons still do not arrive, and
`tests/test_virtual_pad.c` rules out SDL and the mapping (all ten round-trip in
isolation), so what remains is how the running game holds the device.

## 5. RmlUi for player mapping and input bindings

**Not started; nothing vendored yet.** The retained PC controller editor is the
game's own UI and is where feature 2 (controller defaults) lands today. The
intent is a real, modern UI on top: per-player device assignment, rebindable
actions, and the graphics options too.

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

**Partial, and currently a testing shortcut rather than a feature.**
`X2_BOOT_MAP=<map>` boots straight into a level by replacing the boot's intro
script; the mechanism is written up in [`docs/RE/boot.md`](RE/boot.md), which
documents the real `launchMap` INIT handler behind it.

Open: it is documented "for testing only" and skips the whole preamble
unconditionally. Turning it into a player-facing feature means deciding what a
launched game should skip and what it must still do (save selection, difficulty,
party) rather than bypassing all of it.

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
