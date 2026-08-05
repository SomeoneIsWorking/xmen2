---
id: 1
title: gdb line breakpoints lie on the -O2 generated build
status: dead-end
symptom: a gdb breakpoint on a line in xbox/src/recomp/gen/*.c reports a register value that does not match the code above it, or fires several times for one execution of the function
tags: xbox,tooling,gdb
created: 2026-08-05
updated: 2026-08-05
---

The generated C is built at -O2 and the compiler reorders statements across the labels the lifter emits, so a source line is not a point in the execution.

MEASURED, both directions, on sub_0026E740:

- A breakpoint on the `if` line after `ebx = MEM32(esp + 0x1C);` printed g_ebx = 0x007E4000, the CALLER's ebx -- the assignment had not executed yet. Reading it as the loaded value gave a wrong picture of the arguments.
- A breakpoint on a `RECOMP_ICALL_SAFE` line fired three times for a single execution, once with a stale edx that made `MEM32(edx + 0x48)` read 0x00000000, which looked like a NULL vtable slot and was not.

What DOES work:

- Breakpoints on FUNCTION ENTRY (`break sub_0026E740`, `break bridge_NtQueryVirtualMemory`) -- reliable, and `bt` gives a full guest call stack naming every sub_XXXXXXXX frame.
- XBOX_ICALL_WATCH (I012), which traces arguments and return values from inside the ICALL macro where the call actually happens.

Also: do not use $sp as a gdb convenience variable in a script. It is the stack-pointer register alias, and assigning to it fails with 'Attempt to dereference a generic pointer' at the NEXT use, which reads as a problem with the expression rather than with the name. And ebp is `g_seh_ebp`, not `g_ebp` -- there is no g_ebp symbol.
