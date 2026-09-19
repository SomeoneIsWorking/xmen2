# 0171 — the browser's only gameplay route is the one map whose HUD never draws

State items: S020 (platform-neutral touch play), S021 (web product)
Status: cause established and measured; the route now accepts another map and
the browser touch evidence is being taken through it.

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
