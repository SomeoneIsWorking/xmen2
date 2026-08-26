---
id: C268
kind: claim
status: holds
created: 2026-08-26
tags: text,glyphs,prompts,renderer,overrides
depends: src/native/prompt_glyph_draw.c#glyph_loop_string, src/native/prompt_labels.c#x2_probe_004bd720
---

## Claim

The port's composed prompt labels DO reach the exe's glyph loop FUN_005ee780, as wide strings carrying codepoints 0x0090..0x0093 in exactly the shape prompt_labels.c builds. The chain is: x2_override_00619e30 composes the narrow label -> token resolver FUN_004bd720 returns that pointer to its caller -> FUN_005ef2e0 (markup -> wide line buffers) widens it with a plain MOVZX AX,BL at 0x005ef7b3, so the bytes zero-extend unchanged -> FUN_005ee780 walks it. The wide string is FUN_005ee780's FIRST STACK ARGUMENT (entry_esp+4), not EDX. This means step 1 of the docs/RE/text.md renderer plan is sound: an override on FUN_005ee780 can cheaply test each wide string for the port's codepoints and super-call unchanged for every ordinary one.

## Evidence

Boot-direct tutorial run, X2_PROMPT_GLYPHS=1 X2_BOOT_MAP=act0/tutorial/tutorial1 X2_MAX_FRAMES=1200 --no-window --d3d8 --run (scratch/logs/fixedptr-on-err.log): 4581 strings at the glyph loop, 1142 carrying 13704 prompt codepoints, dumped as 0090 0091x5 0092x5 0045 006e 0074 0065 0072 0093 = KEYCAP_LEFT + MIDDLE*5 + REWIND*5 + 'Enter' + KEYCAP_RIGHT. Independently corroborated by the token-resolver census in prompt_labels.c (scratch/logs/resolver-run.log): FUN_004bd720 ran 5448 times and handed our buffer back 2285 times, consumed x1143 at 0x00596f5a (FUN_00596df0) and x1142 at 0x005ef757 (FUN_005ef2e0) -- the 1142 equals the detected string count. Argument binding read out of the retail body: walk at 0x005ee7dc is MOV EAX,[ESP+0x40] / MOVZX EAX,word [EAX], and SUB ESP,0x2c + four pushes puts that at entry_esp+4. ctest -R prompt_glyph_draw drives the override over a real guest stack and requires a positive.

## What would falsify it

a run with X2_PROMPT_GLYPHS=1 through a scenario that composes keycap or pad labels whose PROMPT DRAW line reports 0 prompt codepoints while the prompt-label report shows a non-zero composed count; or a retail-body reading showing FUN_005ee780 takes its wide string somewhere other than entry_esp+4 (which would make the detector read the wrong memory again, as it did under C267)
