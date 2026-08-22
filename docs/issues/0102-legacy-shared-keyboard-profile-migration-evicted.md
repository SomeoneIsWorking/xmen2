---
id: 102
title: Legacy shared keyboard profile migration evicted a player
status: resolved
symptom: Loading old settings where two players share one keyboard profile silently leaves the earlier player without a keyboard
tags: settings,input,migration,keyboard,assignment
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

The assignment-grid migrator called the normal exclusive assignment mutator for every legacy player. Assigning the shared row to the later player correctly evicted the earlier owner, but migration failed to preserve the old many-to-one semantics.

## Resolution

Migration reserves every explicitly referenced source row, keeps the first owner there, and clones the source bindings into the lowest free unreserved row for each later owner. `test_settings` checks deterministic row choice and the cloned binding.
