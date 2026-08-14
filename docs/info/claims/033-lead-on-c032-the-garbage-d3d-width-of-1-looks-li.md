---
id: C033
kind: claim
status: falsified
created: 2026-08-05
tags: 
falsified_on: 2026-08-14
---

## Claim

LEAD on C032: the garbage D3D width of 1 looks like a FIELD-OFFSET error, not arithmetic corruption -- 1 is exactly the value of alchemy.ini's fullScreen=true, i.e. an adjacent config field, and the run dirs are otherwise identical so the values are computed rather than read from differing state.

## Evidence

scratch/run/stock and scratch/run/x2name both contain only alchemy.ini as a real file (everything else symlinks to the same game dir), and the prefix registry holds no XMen2 display keys -- so no external state differs between the run that works and the one that does not. alchemy.ini's [Viewer] section is fullScreen/width/height/colorBits in that order; the recompiled run reports Width=1 where fullScreen=true would be 1, and a bit depth of A8R8G8B8 (32) where stock uses R5G6B5 (16), consistent with reading the config block one field out of position.

## What would falsify it

This is pattern-matching on a single number and could easily be coincidence -- Height=43253760 (0x02942A00) is not obviously any adjacent field, which argues against a simple off-by-one. Confirm or kill it by finding the function that reads the [Viewer] block and difftesting it against the original, rather than by reasoning about the values.

## FALSIFIED 2026-08-14

The field-offset lead depended on C032 being a current defect. C180 shows the current Wine path passes the exact stock structure, so there is no current shifted field to explain; the old value pattern does not establish the historical cause.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
