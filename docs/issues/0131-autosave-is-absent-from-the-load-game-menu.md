---
id: 131
title: Autosave is absent from the Load Game menu
status: resolved
symptom: The transactional autosave exists but the retail Load Game menu does not list it
state_items: S015
tags: save,autosave,load-menu,catalog,user-report
created: 2026-08-27
updated: 2026-08-27
---

## Root cause

The autosave and the retail Load Game screen use two different catalogs.
`x2_autosave_storage_publish` commits the transactional result as
`autosave.save`; `src/save/save_catalog.c` includes that leaf, which is why the
native Continue path can load it. The retail scanner at XMen2.exe `0x0055f2f0`
never consults that catalog: it hardcodes `saveslot*.save`, parses the digit
after the eight-character prefix as slot 0..9, and fills ten fixed 0xa8-byte
metadata records through `0x0055f580`. The row builder at `0x004b0d20` then
constructs its choices from those ten records only.

## What was tried / dead ends

Renaming or copying the autosave over `saveslot0.save` would make it visible by
destroying or shadowing a manual save. Redirecting Continue proved that the
save payload is retail-loadable, but it cannot affect manual Load Game because
that menu never enters the Continue owner.

## Resolution

The shipping adapter keeps the retail manager's ten metadata records intact
and virtualizes only the generic-dialog list that presents them. Binary
evidence fixes that list at ten resident rows: the active page is selected by
`UI+0x403c`, each page is 0x1560 bytes, row text and its two command buffers
are fixed 0x80-byte arrays, and `0x005e97d0` refuses an eleventh row. The native
`load_game_menu_runtime` therefore builds an eleven-entry logical plan (ten
manual leaves plus autosave), projects a sliding ten-entry window into those
resident buffers, and ports `0x005e9d30`'s input-delta poll so focus, commands,
and the projection move together. Manual rows retain their exact retail text,
commands and bit flags.

The autosave row uses the sentinel script choice 10 only inside the dialog.
The native `0x0049f010` bridge intercepts that sentinel before it can reach the
fixed retail manager array, stages the autosave header in record zero, selects
manager byte zero, and redirects the retail `0x0055fcd0` reader to the exact
`autosave.save` leaf. Continue and Load Game now share that exact-leaf
transaction in `exact_save_load`; only Continue arms its automatic success
acknowledgement, so manual Load Game still leaves the retail success dialog for
the player to acknowledge.

Focused policy coverage proves empty, sparse and full 10+1 catalogs, sliding
projection, resident focus synchronization, boundary clamping, and preservation
of all ten manual entries. A source-wiring regression pins the three binary
seams, exact-leaf ordering, sentinel routing, Continue-only auto-ack, and live
report fields.

The product falsifier passed in the isolated profile
`scratch/run/autosave-load-menu.MVpf1k`. The report moved from
`logical=11 resident=10 first=0 selected=0 leaf=saveslot0.save` to
`first=1 selected=10 leaf=autosave.save`, selection recorded
`manager-selected=0 autosave=1`, and `load-success-awaiting-ack.png` showed the
retail `LOAD SUCCESSFUL` dialog with `[ENTER] CONTINUE` still awaiting a
second input. After that manual acknowledgement the destination tutorial map
opened. Hash comparison before selection and after loading showed all ten
manual leaves unchanged. The autosave itself legitimately changed later when
the loaded map's transactional checkpoint fired.
