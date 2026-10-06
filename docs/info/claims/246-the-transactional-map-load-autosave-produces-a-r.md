---
id: C246
kind: claim
status: holds
created: 2026-08-22
tags: save,autosave,continue
depends: src/native/autosave_runtime.c#x2_autosave_override_00484ce0, src/native/autosave_runtime.c#publish_snapshot, src/save/autosave_storage.c#x2_autosave_storage_publish, src/native/continue_runtime.c
---

## Claim

The transactional MAP_LOAD autosave produces a retail-loadable exact autosave leaf without modifying the manual save slot

## Evidence

Two consecutive native product runs on 2026-08-22. Run 1 began with no autosave.save; at frame 428 the live /save report read map-success=2/2 scheduled=2 cancelled-menu=1 idle-polls=64 attempts=1/2 success=1/1 fail=0/1 last=succeeded. autosave.save appeared at exactly 195716 bytes while saveslot0.save retained both size and mtime. Run 2 Boot Continue named load_0055fcd0=autosave.save, crossed 0x0049f140, manager 0x004aed10 mode=3/state=1, deserializer 0x0046e2b0, mode 3->0, and rendered Sanctuary.

## What would falsify it

a generated autosave changes a manual slot, is not exactly the leaf Continue selects, fails the retail mode-3/deserializer chain, or does not restore the saved gameplay state
