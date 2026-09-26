---
id: 191
title: phone continue loaded a level without its heroes
status: open
symptom: on the phone, v0.2.15's boot-to-Continue showed the level partly drawn with no player characters; travelling to the previous level and back fixed it
state_items: S018
tags: android,arm64,save,boot,user-report
created: 2026-09-26
updated: 2026-09-26
---

# 0191 — phone continue loaded a level without its heroes

State items: S018 (Android)

REPORTED BY THE USER, 2026-09-26, minutes after v0.2.15 was installed and
launched on the phone with `boot.mode=continue`: "The level and the characters
didn't load", "Looks like a 'Continue' issue", "It fixes itself if I go back to
the previous level and go back again."

The phone's first frames (a dockside level, fence and water, HUD and four party
portraits present) show the camera over empty ground: no hero is drawn.

## What was ruled out

Every run below used `boot.mode=continue`, the path the phone took, and reached
a rendered level with its heroes:

| Host | Code | Save | Result |
| --- | --- | --- | --- |
| Linux x86-64, headless | v0.2.16 | desktop autosave (Sanctuary) | level and party drawn |
| Android x86-64 AVD, API 35 | v0.2.16 debug | AVD autosave (tutorial) | level and party drawn |
| Same AVD, arm64-v8a APK via `libndk_translation` | v0.2.16 debug, ARM64 JIT | AVD autosave | level and party drawn |

- The x86-64 AVD has the phone's binary128 `long double`, so the x87 `_ftol`
  change (72aece4) runs there in the phone's register layout.
- The translated arm64 run executes the ARM64 JIT's own output, including the
  a2eff95 store-to-load forwarding, flag tuple and inline FNSTSW, and the
  9d004af dead-flag IMUL.
- The ARM64 real-code differential (`x86p_jit_coverage` under `qemu-aarch64`
  over the X-Men 2 corpus) compares 26,823 blocks with 0 divergences at
  x86port 3f0957c.

## What is not covered

The phone's own save (a later, dockside level) and a real ARM64 core with weak
memory ordering. The translation layer runs on x86's stronger ordering, so a
race between a host worker and guest code on shared guest memory would not
appear there.

## Next

Ask whether it recurs on v0.2.16. If it does, the discriminating input is the
phone's `autosave.save`, which a release build cannot export today.
