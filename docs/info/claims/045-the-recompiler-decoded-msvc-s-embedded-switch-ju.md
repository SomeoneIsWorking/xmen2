---
id: C045
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/xboxrecomp.lock
---

## Claim

The recompiler decoded MSVC's embedded switch jump tables as instructions, desynchronising the linear sweep so the switch targets got no basic block, no label, and translator.py deleted their gotos to make the C compile. That silently turned the CRT memcpy (sub_003D5890) into a no-op for any size not a multiple of 4.

## Evidence

55 'dead code, label not in function' comments in the generated output, all in sub_003D5890 and sub_003D7DB0. Excluding the table byte-spans from decoding and re-decoding takes it to 0 jumps deleted across 21,909 functions, and the switch now reads 'if (_jt == 0x003D59ECu) goto loc_003D59EC;'. In the run, the CRT heap creation that consumed a memcpy'd 0x30-byte parameter block goes from returning 0 to returning handle 0x00F80063, and NtAllocateVirtualMemory(1MB) appears.

## What would falsify it

a switch whose table is NOT contiguous from the jmp's displacement, or whose entry count _read_jump_table mis-infers, would still desync -- the drop counter is the detector
