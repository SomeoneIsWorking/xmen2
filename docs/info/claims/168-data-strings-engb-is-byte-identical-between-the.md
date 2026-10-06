---
id: C168
kind: claim
status: holds
created: 2026-08-12
tags: assets,prompts
---

## Claim

Data/strings.engb is byte-identical between the Xbox disc and the PC install (53,487 bytes, md5 ee1a8c03a0dfebc58ee74df43e98f13a), so the Xbox button prompts do not come from the strings table. strings_svs.engb differs only by an appended Xbox Live sentence.

## Evidence

md5sum of scratch/xbstrings/data/strings.engb (extracted from scratch/xbox_iso/assetsfb.wad) against GAME_PC_DIR/Data/strings.engb; diff of strings -n 3 output for the _svs pair shows one changed line.

## What would falsify it

if a different language table (freb/gerb/itab/spab) differs in a way that carries glyph codepoints, the strings table is involved after all

## Falsifier tested, with its blind spot

Ran it: the Xbox WAD carries `strings.{engb,freb,gerb,itab,spab}`, this PC
install carries **only `engb`** (its localisation lives in `igct*.bnx`, not in
per-language strings tables). So the falsifier is testable for exactly one of
the five pairs, and that pair is identical. **Blind spot: the four non-English
pairs cannot be compared on this install at all** -- not "compared and found
equal". A localised PC install would be needed to close that, and it would only
matter if a non-English build emitted glyphs the English one does not, which
nothing suggests.
