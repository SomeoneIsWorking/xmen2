---
id: I069
kind: instrument
status: trusted
created: 2026-08-26
---

## Instrument

src/native/prompt_glyph_draw.c -- the glyph-loop prompt detector (override on XMen2.exe FUN_005ee780), with tests/test_prompt_glyph_draw.c

## Validated by

NOW VALID, AFTER IT LIED. It reads the wide string from FUN_005ee780's FIRST
STACK ARGUMENT (RD32(esp+4)), derived from the retail body: the walk at
0x005ee7dc is MOV EAX,[ESP+0x40] / MOVZX EAX,word [EAX], and the prologue's
SUB ESP,0x2c plus four pushes puts that slot at entry_esp+4.

It first shipped reading C->edx, which FUN_005ee780 overwrites from [EDI+0x8]
at 0x005ee797 before reading. That reported 0 prompt codepoints over 4541
strings across a pack-on/pack-off A/B and produced claim C267 ('the port's
labels never reach the glyph loop'), which was FALSE. With the pointer
corrected the same scenario yields 1142 strings carrying 13704 codepoints.

WHY THE UNIT TEST DID NOT CATCH IT: the test set cpu.edx itself, so it
validated the classifier against an argument binding it had assumed rather
than one taken from the guest. It now builds a real guest stack -- return
address at ESP, string pointer at ESP+4 -- and calls the override the way the
engine does, so a wrong binding fails the test instead of passing it.

The discriminator has been run against BOTH classes: the test feeds a composed
keycap label and both range boundaries and requires a positive, and the live
run distinguishes our 0x0090..0x0093 from the engine's own above-256 control
words (9d28, 01f2, 08e2) and the legal screen's 0x00bd. Corroborated
independently by the token-resolver census in prompt_labels.c: 1142 consumed
at 0x005ef757 equals 1142 strings detected.

LESSON FOR ANY DETECTOR ON A GUEST BODY: read the argument out of the
retail prologue, never from a register a comment claims. Wide text is common
enough in nearby registers that a wrong pointer decodes as plausible strings
and reads like a working instrument.

## Known failure modes

(none recorded yet)
