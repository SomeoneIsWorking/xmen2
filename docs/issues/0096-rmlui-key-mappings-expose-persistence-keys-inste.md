---
id: 96
title: RmlUi key mappings expose persistence keys instead of action labels
status: resolved
symptom: keyboard settings show Ally, TargetLock and SreenGrab instead of Energy Pack, Health Pack and Screenshot
tags: pc,native,input,rmlui,bindings,labels,user-report
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

`settings_document.cpp` rendered `input_binding_row_name()`, but that function
returned the identifiers `FUN_0061b030` uses for registry persistence. Those
keys deliberately retain executable spellings such as `TargetLock`, `Ally`,
and typo `SreenGrab`; they do not describe the actions to a player. The UI had
conflated storage identity with presentation.

## Shipped semantics

The PC English localization in `igct.bnx` maps all 42 storage keys to display
text. In particular, `Ally=Energy Pack`, `TargetLock=Health Pack`, and
`SreenGrab=Screenshot`. The power tutorial separately says `$ALLY` replenishes
Energy, so the first mapping is not the still-open question of which PC action
corresponds to Xbox White.

## Resolution

`src/input/binding_rows.c` is one descriptor owner for both contracts. RmlUi
uses its display labels and the live probe uses its storage keys. The focused C
test calls the production API and pins the three discriminators plus bounds;
`tools/binding_rows.py` compares all 42 keys with `XMen2.exe` and all 42 labels
with `igct.bnx`.
