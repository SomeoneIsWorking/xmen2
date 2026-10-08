---
id: 193
title: The game turns on the phone's auto-rotate
status: resolved
symptom: With auto-rotate off on the HONOR 600 (VKJ-NX9), playing the APK leaves the system auto-rotate setting on; reported repeatedly by the user
state_items: S018
tags: android,orientation
created: 2026-10-08
updated: 2026-10-08
---

# 0193 — The game turns on the phone's auto-rotate

## Reproduction

Auto-rotate off (`settings get system accelerometer_rotation` = 0, window
`USER_ROTATION_LOCKED`). Open the game, return to another app. Expected: the
setting is still 0. Reported: it is on.

## Findings

- Nothing in `android/` or the SDL Java layer writes `accelerometer_rotation`.
  Both activities declare `screenOrientation="landscape"`, and
  `SDL_HINT_ORIENTATIONS` "LandscapeLeft LandscapeRight" makes SDL request
  `SCREEN_ORIENTATION_USER_LANDSCAPE`.
- `dumpsys settings` shows the setting written by `pkg:android` (generation 9),
  so the write goes through the system, not the app's own package.
- Cause: the launches, not the app. `adb shell monkey -p <package> ... 1`
  sends rotation events by default, which unlock rotation. Measured
  2026-10-08: every monkey launch (X-Men, Terraria, Brawl Stars) set
  `accelerometer_rotation=1`; the same games opened from the launcher, and
  X-Men opened with `am start -n com.someoneisworking.xmen2/.XMen2SetupActivity`,
  left it 0 throughout.

## Resolution

Launch on a device with `am start`, never `monkey`. No app change.
