---
id: 76
title: A vtable function Ghidra left as a 1-byte stub traps the port minutes into gameplay
status: resolved
symptom: the game dies during gameplay with a generic illegal-instruction crash and no crash log; x86_untranslated: reached guest 0x00424240 FUN_00424240 -- blocked by: no decoded instructions
tags: recomp,ghidra,export,crash,gameplay,disassembly,misaligned
created: 2026-08-15
updated: 2026-08-15
---

## Symptom

Reported as "crashes in gameplay if you move or switch characters", with "no
crash log, just a generic C crash, illegal something". Reproduced on an
interactive run: the log carries

    x86_untranslated: reached guest 0x00424240 FUN_00424240 -- blocked by: no decoded instructions

immediately after the tutorial's opening conversation ends, i.e. as gameplay
starts.

## Cause

`scratch/recomp/XMen2.json` carried 0x00424240 with `size 1` and ZERO
instructions, and a function `FUN_00424242` starting TWO BYTES INTO IT with 102
instructions.

0x00424240 is a genuine function -- a textbook MSVC thiscall prologue --
reached through a vtable slot in `.rdata` at 0x00682e40, whose neighbours are
all genuine function entries:

    0x00424240  sub  esp, 0x14        (83 ec 14)
    0x00424243  push esi              (56)
    0x00424244  mov  esi, ecx

Ghidra decoded the region starting at 0x00424242 first, reading the tail of
that prologue as `ADC AL,0x56` (bytes 14 56) and re-synchronising at 0x00424244
-- so the misaligned body is otherwise identical to the real one. When the
address was later seeded from its vtable slot there was one byte of room in
front of an already-defined function, and the seed became a stub with nothing
in it. The recompiler translates a stub into a trap, and the trap fires the
first time the game makes that virtual call.

## Denominators

Measured over the whole export (16,453 functions):

- 4 functions decode to ZERO instructions: 0x00424240, 0x0065006c, 0x0065006d,
  0x00672269. Only 0x00424240 is known to be reached.
- 0 function entries fall strictly inside another function's decoded
  instruction, and 0 bytes are claimed by more than one function -- so there is
  no OTHER silently-misaligned code in this module. The damage is exactly these
  four traps.

Of the other three: 0x0065006c and 0x0065006d are one byte apart and their only
"references" are 54 and 15 dwords in `.rsrc`, which is resource bytes matching
by coincidence rather than pointers -- probably bogus seeds. 0x00672269 is an
SEH unwind stub (`mov esp,[ebp-0x18]; jmp`) referenced from `.rdata`.

## Why it was not caught

`tools/ghidra_export.sh`'s post-export check counted functions and instructions
and refused a ZERO-FUNCTION export, but accepted a function with zero
INSTRUCTIONS. So a failed decode shipped as a build that runs, and surfaced
hours later as an unexplained crash in gameplay.

## Fix

- `tools/ghidra_scripts/RepairStubs.py` (new): removes the stub and every
  function beginning inside the span it should own, clears the code units,
  re-disassembles from the true entry, recreates the function, and puts back
  any neighbour that is still an instruction boundary. `SplitFunction.py`
  cannot do this -- it repairs an address swallowed INSIDE a function and bails
  with "already a function start", because here the stub IS the function start
  and what is wrong is the function after it.
- `tools/ghidra_export.sh --repair-stubs [ALL|<addrs>]` runs it.
- The post-export check now REFUSES an export carrying any zero-instruction
  function, and prints the count with its denominator even when it is zero.

### Resolution (2026-08-15)
Fixed. tools/ghidra_scripts/RepairStubs.py repaired all four zero-instruction functions; tools/ghidra_export.sh --repair-stubs wires it in; the post-export check now REFUSES any function with no instruction at its entry -- the gate that let this ship. After re-export and re-emit: 16450 functions, 0 with no instruction at their entry, emitter reports 0 untranslated (was 4). Verified by a 6200-frame run reaching gameplay that exits 0 with zero x86_untranslated hits (scratch/logs/postfix.log). See C197.
