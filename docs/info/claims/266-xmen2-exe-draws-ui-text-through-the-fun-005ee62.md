---
id: C266
kind: claim
status: holds
created: 2026-08-26
tags: text,glyphs,fonts,renderer,overrides
depends: src/native/text_caller_probe.c#x2_probe_005972a0, docs/RE/text.md
---

## Claim

XMen2.exe draws UI text through the pair `FUN_005ee620` (binds the font atlas
via the font table's getTexture and caches scales) and `FUN_005ee780` (walks a
wide string; for every wchar < 256 reads the glyph record at
`wchar*0x1c + [drawobj+0x18]`, builds a quad from its metrics and UV floats,
submits it through `FUN_005ee400`, advances the pen by the record's advance).
Measurement goes through font-table vtable `+0x38` = `FUN_00597c90`, which
accumulates `word[glyph*0x1c+4]` per narrow byte. No other code path reads
glyph records: an address-entry trace over `0x596000-0x5a0000` during a 400-frame
tutorial run shows exactly these cluster functions hot (getRecord x3211,
getTexture x4931, measurer x645, tokenizer x212).

## Evidence

`X2_TEXTURE_PROBE=1` run (`scratch/logs/probe-err.log`): 2,439 getTexture
calls, eight distinct return sites, the two hot ones (`0x005ee63d`,
`0x005ee663`, x1207 each) inside `FUN_005ee780`. Ghidra decompile
(`scratch/re-ghidra/decomp-drawer.c`) shows the char loop, the
`wchar*0x1c + [obj+0x18]` record lookup, the UV float reads with v-flip, and
the `FUN_005ee400` submission. The retained aggregate counts are recorded above.
Full write-up: `docs/RE/text.md`.

## What would falsify it

A prompt-glyph override on `FUN_005ee780` that super-calls for ordinary
strings yet leaves visible text unchanged on screen while some OTHER path
still draws glyphs (e.g. a second renderer for conversations or HUD); or a
string drawn with a glyph whose record was never read through
`wchar*0x1c + [obj+0x18]`.
