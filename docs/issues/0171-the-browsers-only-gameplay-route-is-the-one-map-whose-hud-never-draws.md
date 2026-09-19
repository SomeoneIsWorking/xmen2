# 0171 — the browser's only gameplay route is the one map whose HUD never draws

State items: S020 (platform-neutral touch play), S021 (web product)
Status: resolved. Both causes are fixed and the browser now publishes touch
to the pad — measured through the product's own census.

## Symptom

A browser run cannot show an on-screen control, so touch cannot be exercised
there at all. Every heartbeat of an eight-minute run reported the same thing:

```
[touch] [HB] no contact reached the port this run -- touch_controls=AUTO,
        source says not touch, gate never-seen, a window was present.
[touch] [HB] 32 of 32 dropped before routing: 0 with no window,
        32 with the overlay hidden (touch_controls=AUTO, source says touch,
        gate never-seen)
```

The overlay is gated on `x2_gameplay_control_state`, and that gate never
opened: 137 heartbeats, `never-seen` throughout.

## Cause

The gate's signal is the retail game's own HUD decision — the native override
on `0x005a43d0`, CHud's party-selector panel (`src/native/touch_hud_runtime.c`).
That is deliberate and it is not the defect: it cannot drift from the retail
rule because it *is* the retail rule.

The defect is the route. `src/web/web_main.cpp` hard-coded the gameplay test to
`act1/deadzone/deadzone1`, and **the Dead Zone map does not draw the party
HUD.** Measured across every recorded run of this port, native and browser,
on the same `X2_BOOT_MAP` direct-load mechanism:

| run | map | visible party draws | gate |
| --- | --- | --- | --- |
| `scratch/run/cases/cutscene-skip` | `act0/tutorial/tutorial1` | 19 | active |
| `scratch/run/cases/deadzone-render` | `act1/deadzone/deadzone1` | 0 | never-seen |
| `scratch/web/native-dz`, `-dz2`, `-window-luma2` | `act1/deadzone/deadzone1` | 0 | never-seen |
| browser `#test-play` (this issue) | `act1/deadzone/deadzone1` | 0 | never-seen |

The tutorial map reaches the same override through the same route, so this is a
property of the map, not of the gate, the override or the direct-load boot. The
Dead Zone map is still drawing HUD elements — the same run submits 236 portrait
draws — but not through the party-selector panel the gate watches.

`#test-play` is the browser product's only gameplay route, so the browser could
reach exactly one map, and it was the one that keeps the gate shut.

## Fix

`src/web/web_request.{hpp,cpp}` gained `--test-map=<path>`, scanned over the
whole of argv like the other entry requests, defaulting to the Dead Zone map.
The page already forwards `?arg=` values, so a maintainer reaches another map
with `?arg=--test-map%3Dact0/tutorial/tutorial1` and no page change. The Android
bridge has taken its boot map from its caller all along; this is the browser
catching up.

`tests/test_web_request.cpp` checks the default and the override in both
directions: a reader that always returned the default passes every "is it Dead
Zone" case, which is exactly the bug. Verified by making it do that — three
checks fail.

## Not fixed here

Why the Dead Zone map draws no party HUD is a separate question about that
map's content. It is a diagnostic map reached by a diagnostic boot; nothing
says it must present a party. It is recorded here because it silently made an
entire platform's touch support unmeasurable, not because the map is wrong.

## What the route then found — the touch chain had no pad

With the tutorial map reachable, the browser sweep finally landed on drawn
controls, and the product's own census reported the real defect:

```
  mode AUTO, source touch, gate active
  contact events 0 -> 48  (+48)
  zone actions   0 -> 72  (+72)
  pad buttons    0 -> 0  (+0)
  pad axes       0 -> 0  (+0)
  refused by the pad: 4 button(s), 32 axis change(s)
touch: could not move virtual axis leftx: this run has no synthetic pad to
       press (X2_VIRTUAL_PAD is unset, or the pad attached but could not be
       opened).
```

The on-screen controls publish through the synthetic SDL pad — that is what
`dinput_pad_virtual_set/release` are for — and **nothing attached that pad
except the `X2_VIRTUAL_PAD` diagnostic and the Android bridge doing it by
hand** (`src/native/android_bridge.cpp`). Setting it there was the only reason
Android's touch worked; on every other platform the overlay drew, the zones
lit up, and each press was refused.

The touch owner now attaches its own pad
(`dinput_pad_virtual_attach_for_touch`, called from `touch_runtime.cpp`'s
publish path), the Android special case is gone, and the census reports the
pad's absence by name instead of leaving a row of refusals with no cause.

`tests/test_touch_runtime.cpp` no longer attaches a pad for itself: it checks
that none exists before the first contact and that one exists after, so it
proves the product supplies the pad rather than proving the chain works given
one. Removing the `ensure_pad()` call fails it; verified.


## Verified

`tools/web_touch_play.py`, same sweep, against the package built with both
fixes, tutorial map, 390x844 emulated touch device:

```
  mode AUTO, source touch, gate active
  contact events 0 -> 48  (+48)
  zone actions   0 -> 62  (+62)
  pad buttons    0 -> 4  (+4)
  pad axes       0 -> 32  (+32)
  refused by the pad: 0 button(s), 0 axis change(s)
  dropped before routing, cumulative: 0 of 48
  player one: the touch pad was claimed
```

A touch on a drawn control reaches the pad in a browser, and the pad is
player one's, which is what makes the guest poll it. The page kept every
gesture (scrollY 0) — issue #170's fix holding under a real sweep.

What this does not show is the character moving on screen. The census sees as
far as the pad; `tests/test_touch_runtime.cpp` reads the presses back off a
real SDL gamepad, so the link past the pad is covered there rather than here.
A visual confirmation on a device remains part of S020's gap.
