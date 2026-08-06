---
id: 25
title: Native --d3d8 run: a callback pointer read from object+0x40 is a code fragment, not a function
status: resolved
symptom: x86_dispatch: no recompiled body at 0xc0850b74, dispatched from XMen2.exe 0x00656856. Reached after the longjmp resume, during startup, on every --d3d8 run.
tags: pc,native,rc-exe,dispatch,setjmp
created: 2026-08-06
updated: 2026-08-06
---

## The call site, read rather than guessed

`XMen2.exe 0x00656843`, thirteen instructions:

    00656843  PUSH ESI
    00656844  MOV ESI,dword ptr [ESP + 0x8]     ; the object, first argument
    00656848  MOV EAX,dword ptr [ESI + 0x40]    ; a callback it holds
    0065684b  TEST EAX,EAX
    0065684d  JZ 0x00656858                     ; NULL is expected and handled
    0065684f  PUSH dword ptr [ESP + 0xc]
    00656853  PUSH ESI
    00656854  CALL EAX
    00656856  POP ECX

So the object at `[ESP+8]` holds a callback at `+0x40`, the guard for NULL is
there and passes, and the value called is `0xc0850b74`. That is not a NULL
that slipped through -- it is a non-zero value that is not a function.

Its bytes read as code: `74 0b 85 c0` is `JZ +0x0b; TEST EAX,EAX`. So a
fragment of an instruction stream is sitting where a function pointer belongs.

## What is NOT established

Whether the object is uninitialised, whether it was overwritten, or which
object it is. Nobody has identified the type or found who fills `+0x40`.

## The ordering fact, and what it does NOT imply

It happens AFTER the longjmp resume (`crt: longjmp RESUMED into a generated
body`), so the resume is the obvious suspect. One specific version of that
suspicion has been CHECKED AND RULED OUT: C says a local modified between
setjmp and longjmp is indeterminate afterwards, which would make a generated
body resume with garbage in its temporaries. Reading the emitted code shows it
cannot apply -- every value lives in the `CPU` struct through the `C` pointer,
and the only locals are block-scoped temporaries written before they are read
(`{ uint32_t _a, _b, _r; ... }`). `_retaddr` and `_x86_fn_ep` are set at entry,
before any setjmp, and never modified.

What has NOT been ruled out: host-side state owned by the frames the longjmp
destroyed is not unwound -- the ark scratch-stack pointer is one such -- and
whatever raised the unwind in the first place (issue #24) may have left the
object in this state before any of this.

## Where to look next

Name the object. `0x00656843`'s caller passes it as the first argument, and
`0x00656756` -- called from the setjmp function at `0x006460e7` in issue #24 --
is in the same neighbourhood, so the two are probably the same subsystem.

### Note (2026-08-06)
ROOT CAUSE FOUND, and it is not what this issue guessed. The callback at object+0x40 is the game's OUT-OF-MEMORY handler: XMen2.exe 0x0065ce60 calls it ONLY when malloc has returned NULL. So the object is not uninitialised -- the handler was simply never installed, because the game never expected to reach that path.

What made malloc fail is issue #26: it was handed 0x700ff678, a guest stack address, as a size. This issue is therefore a SYMPTOM of #26, and chasing the callback was chasing the error reporter rather than the error.

### Resolution (2026-08-06)
Not a defect in its own right: the callback at object+0x40 is the game's out-of-memory handler, reached because malloc failed. The real fault is issue #26.
