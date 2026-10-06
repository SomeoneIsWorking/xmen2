---
id: C231
kind: claim
status: falsified
created: 2026-08-21
tags: input,prompts,gameplay
depends: src/native/dialog_prompts.cpp#x2_override_00629bf0, src/native/pad_glyphs.cpp, src/native/prompt_labels.cpp
falsified_on: 2026-08-22
---

## Claim

A connected controller makes all eight hardcoded PC tutorial popups retain their localized controller-authored asset text, while keyboard keeps the PC override; a natural switching_hint run measured 7,259/7,259 pad labels and zero keyboard/original prompt names

## Evidence

Windowless natural act0/tutorial/tutorial1 switching_hint run: Tutorial dialog text 1 controller asset, 0 PC override, 8 unrelated lookups; Xbox prompt rows 7259/7259 pad; docs/issues/0087 and tests/test_dialog_prompts.cpp record the trace and ABI checks

## What would falsify it

A controller-connected natural tutorial popup selects an igct.bnx PC string, any encountered pad-bound prompt reports an original/keyboard name, or the scoped localization wrapper fails its ABI/stack unit test

## FALSIFIED 2026-08-22

The product policy changed from any-connected-controller to the assigned player's last active source. A connected but unassigned controller, or a keyboard-active player who owns both devices, must retain keyboard prompts; connection count is no longer the selector.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
