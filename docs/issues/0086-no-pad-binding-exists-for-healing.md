---
id: 86
title: No pad binding exists for healing
status: open
symptom: the healing action has no binding at all with a pad connected -- there is no button that uses a health item
tags: pc,native,input,pad,bindings,gameplay,user-report
created: 2026-08-19
updated: 2026-08-19
---

REPORTED BY THE USER, 2026-08-19, from a real play session with a pad.

Healing (using a health item) has **no pad binding at all**. Not a wrong
button -- nothing is assigned, so the action is unreachable with a pad in hand.

## Why this is worth its own entry

It is almost certainly the same missing-rows cause as #85 (R + face button
does nothing), and if one change fixes both they resolve together. It is
logged separately because it is a sharper test case: healing is a SINGLE
action on a SINGLE physical input on the Xbox build, with no modifier
involved, so it isolates "the preset has no row for this action" from "the
preset cannot express a modifier combination". Whichever of those two is true
for #85, this entry answers half of it on its own.

There is also a concrete prior finding to start from rather than re-deriving:
the Xbox binding recovery (C188, `tools/xbe_query.py`) already established
that Xbox `sub_00088680` and PC `FUN_0047a140` keep equivalent separate
`HEALTH_ITEM` / `ENERGY_ITEM` consumption branches, and C191 found that those
two names are item-TYPE strings in the table at `0x0053FEBC` that **no
instruction references**. That was recorded as an aside during the defaults
work; it is now the direct question. The consumption branch exists in the PC
build, so what is missing is the binding row that reaches it, not the feature.

## What the next session should do first

1. `python3 tools/info.py brief health item binding` and read C188/C191 before
   touching anything -- the Xbox side of this is already recovered.
2. Enumerate the port's installed rows (`tools/x2ctl.py input`,
   `tools/binding_rows.py`) and confirm by NAME that no row targets the health
   action. A row list with its count is the evidence; "I did not see one" is not.
3. Find what the PC build's own editor calls this action, and whether the PC
   default keyboard map binds it -- if the keyboard has it and the pad does
   not, the row exists in the game and only the port's preset omits it, which
   is a small fix in `src/native/xbox_defaults.c`.
