---
id: 99
title: Continue and autosave require observed retail save-manager transitions
status: investigating
symptom: A real Continue plus transactional autosave cannot be hooked faithfully until the live game proves main-menu show/build ownership, arbitrary-leaf load dispatch, save mode/state lifecycle, map nosave gates and extraction success.
tags: save,continue,autosave,menu,re,instrumentation
created: 2026-08-22
updated: 2026-08-22
---

## Current gap

Pure production policy now owns exact latest-save selection and a debounced
autosave queue, while the default live run plus `tools/x2ctl.py save` observes
every candidate retail seam with denominators. No Continue/menu mutation or
autosave dispatch is installed yet.

## Required observation

One targeted live run must show whether `CMenuMain` 0x005c9970 builds on each
entry or is cached while 0x005c9260 runs per show; whether
`ui/menus/main.engb` opens; and an arbitrary leaf reaching 0x0055fcd0 ->
manager 0x004aed10 -> deserializer 0x0046e2b0 -> callback 0x0049f150,
including the metadata branch and manager fields. It must also show the save
lifecycle through 0x004aeb80 / 0x004b15b0 state 27 / 0x004b1746 /
0x004b177a back to idle. Map 0x00484ce0 with the +0x221 `nosave` bit, zone
request 0x0049f860 and extraction `saveloadProcess(4)` provide candidate stable
checkpoint evidence.

## Falsifier / next action

Run the default product, drive one manual save and load plus a
map/zone/extraction transition, then capture `tools/x2ctl.py save` (also
included in `probe`). Any expected point remaining 0/N or 0/0 names the missing
RE observation; do not replace it with a catalog-only Continue or a guessed
manager state write. (`X2_SAVE_TRACE=0` is the explicit opt-out.)
