---
id: 137
title: AppImage loads project env and bypasses first-run Browse
status: resolved
symptom: Launching the AppImage from a directory under the source checkout starts the game without showing setup because x2native loads the project .env before parsing --appimage
tags: appimage,setup,environment,release
created: 2026-08-30
updated: 2026-08-30
state_items: S017
---

## Root cause

`x2native` loads the nearest project `.env` before parsing its launch mode, so
the packaged `--appimage` path can acquire `GAME_PC_DIR` from a developer
checkout and skip its sole player setup owner.

## Proper fix

Parse launch options first and categorically disable project `.env` loading for
`--appimage` (including Android), while preserving it for direct developer
launches. Add a regression at the shipping option boundary and exercise the
actual AppImage prompt plus Browse picker under an isolated XDG profile.

### Resolution (2026-08-30)
Root cause: x2native loaded project .env before parsing --appimage, creating a second setup authority. The native entry point now parses options first and skips project .env for packaged AppImage/Android launches while preserving developer launch behavior. x2native_options and appimage_env regressions pass; the rebuilt AppImage passed tools/appimage_setup_probe.py for prompt, Browse, selection, and isolated OS-config persistence, while public v0.1.1 failed the same discriminator.
