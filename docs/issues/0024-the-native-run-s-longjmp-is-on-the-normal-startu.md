---
id: 24
title: The native run's longjmp is on the NORMAL startup path, not an error path
status: open
symptom: crt: MSVCR71!longjmp is not implemented natively. Reached every run of --d3d8 --run, immediately after renderer init clears, from XMen2.exe 0x0064608d unwinding to a setjmp taken at 0x00646115.
tags: pc,native,crt,setjmp,rc-exe,graphics
created: 2026-08-06
updated: 2026-08-06
---

## What is known, and how

`_setjmp3` is implemented as a STOPGAP (src/native/crt.c): it records the guest
register file and returns 0, so a protected call proceeds. `longjmp` stops by
name, because resuming needs a host frame that is still alive and an import
stub's is not.

That stopgap was written on an ASSUMPTION -- that every longjmp here is a rare
error path, since libIGLua takes a setjmp per pcall and a longjmp only on
failure. **Running it refuted that immediately.** The longjmp fires on every
run, right after renderer initialisation completes, so it is part of ordinary
startup control flow rather than a failure being reported.

The diagnostic was then made to name both halves, which is where the useful
part is:

    longjmp was called from 0x0064608d (in no recompiled body this host can name)
    the setjmp it unwinds to was called from 0x00646115

## Two facts worth keeping

1. **The setjmp side is inside XMen2.exe 0x006460d1**, a 187-instruction
   function that stages a buffer at `[EBP-0x274]`, stores `0x646072` and
   `0x663b19` into it, and then calls `0x0067281a` with two arguments -- the
   shape of `_setjmp3(jmp_buf, 0)`.

2. **The longjmp side, 0x0064608d, is in NO DETECTED FUNCTION** -- 16106
   functions were scanned and none contains it. It sits at 0x646072..0x6460d1,
   which is exactly the address the setjmp side stored into its buffer. So it
   is a funclet reached only through that stored pointer: the same class of
   undetected code as the C++ catch funclets in C123, and a discovery-loop
   input.

## What has NOT been established

Whether this is MSVC `__try`/`__except` (the funclet shape suggests it), the
program's own error handling, or Lua. Nobody has read 0x646072's body or found
what raises the unwind. If it IS an exception being handled, then something
upstream faulted and THAT is the real defect -- the message says so, but no
evidence either way has been gathered.

## The proper fix for the setjmp half

`tools/recomp.py` special-cases a call to `_setjmp3` and emits an inline host
`setjmp` in the GENERATED BODY rather than a call to an import stub. The frame
a host longjmp resumes into must still be alive, and the guest's setjmp call
site is in the middle of a generated C function -- so the generated function is
the only place the host setjmp can legally live. Translator work plus a
regeneration; nothing about it is subtle.
