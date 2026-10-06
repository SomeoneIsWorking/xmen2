---
id: 56
title: Every guest thread got 0xDEADBEEF as its argument: two return addresses were pushed
status: resolved
symptom: *** SIGSEGV at 0xdeadbeef (not an import slot), EAX=deadbeef, one instruction into a freshly started thread; the ring's last entry is the ENTER of the thread's start routine with <- 0xdeadbeef
tags: native,threads,abi,pc
created: 2026-08-12
updated: 2026-08-12
---

## Symptom

Selecting NEW GAME and a difficulty in the menu -- the first time the game
had ever been driven that far -- started a thread and crashed immediately:

    *** SIGSEGV at 0xdeadbeef (not an import slot)
    [REGS] eax deadbeef ...
    addr2line: fn_libIGCore_10075da0

and the boundary ring's last line is

    enter  esp 7138c814  libIGCore.dll!0x10075da0 FUN_10075da0  <- 0xdeadbeef

## Cause

`thread_main` in `src/native/threads.c` laid out the new thread's stack as
"the argument, then the return address the thread routine returns to":

    C.esp = stack_base + stack_bytes - 16;
    WR32(C.esp + 4, t->arg);
    WR32(C.esp, 0xDEADBEEF);
    x86_guest_call(&C, t->start);

But `x86_guest_call` pushes the return address ITSELF -- that is the whole
reason it exists (see the comment on it in `x86rt_native.c`, which was
written after a missing push corrupted the stack across 51 static
constructors). So the stack came out as

    [esp]   0xDEADBEEF   (pushed by x86_guest_call)
    [esp+4] 0xDEADBEEF   (pushed here, believing it had to be)
    [esp+8] the argument

and a `DWORD WINAPI proc(void *)` reads its parameter as `[EBP+8]`, which is
`entry_esp+4` -- the SECOND sentinel. Every guest thread that used its
argument therefore got 0xDEADBEEF and dereferenced it.

## Why it took this long to show

The threads the game had started until now -- the movie decoder and its two
siblings -- do not dereference their parameter, so they ran correctly with a
nonsense value in it for the whole life of the port. The first thread that
reads `[EAX]` off its argument is started when a level begins loading, which
is the first thing that happens after the main menu, which is the first thing
input could reach.

## Fix

The argument goes at `[ESP]` and nothing is pushed above it;
`x86_guest_call` supplies the return address. One line, and the comment now
says why there is only one.

## Lesson

Two layers each doing the "obviously necessary" thing. The push here was
correct in isolation and correct in `x86_guest_call`; only the composition
was wrong, and nothing checked it because the value being wrong is invisible
to any thread that ignores its argument.
