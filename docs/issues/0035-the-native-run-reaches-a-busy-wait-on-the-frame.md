---
id: 35
title: The native run spins in the frame limiter -- FILD qword was translated as a 32-bit load, so the engine's 64-bit timer wrapped at 2.147 s
status: resolved
symptom: x2native --d3d8 --run presents frames and then stops presenting while still executing; the boundary ring shows only igLongTimer::getTimeAsLong / igWin32LongTimer::getTimeOfDay / FUN_0055b470 / FUN_0055b610
tags: pc,native,timing,main-loop,rc-exe,recompiler,x87
created: 2026-08-07
updated: 2026-08-07
---

## Root cause

`tools/recomp.py` translated **every** integer x87 memory operand at 32 bits:

```python
def fsrc(o, integer):
    if integer:
        return "RDI32(%s)" % o.addr()      # regardless of o.size
```

So `FILD qword ptr [ESP]` read the **low dword** of a 64-bit value and sign-extended it. `FISTP qword ptr` had the same defect on the store side (`WR16 if size == 2 else WR32`).

The engine's clock is a 64-bit nanosecond count. `igWin32LongTimer::getTimeOfDay` turns `QueryPerformanceCounter` into ns, `igLongTimer::getTimeAsLong` rebases it, and `XMen2.exe FUN_0055b470` converts it to float seconds with

    FILD qword ptr [ESP] ; FMUL float ptr [0x0069a7b0]   (= 1e-9)

Read 32 bits wide, that value **wraps negative at 2^31 ns = 2.147 seconds**. The game's frame limiter is

    00401ff0  CALL 0x0055b610                 ; the timer singleton
    00401ffa  CALL dword ptr [EDX + 0x28]     ; -> FUN_0055b470, now
    00401ffd  FST  float ptr [ESP + 0x14]
    00402001  FSUB float ptr [ESI + 0x1c]     ; now - lastFrameStamp
    00402004  FCOMP float ptr [ESI + 0x18]    ; vs 1/60 s
    0040200c  JNP  0x00401ff0

with no guard for a negative delta -- correct on hardware, where the clock never goes backwards. Once `now` wrapped, `now - last` stayed negative forever and the loop never exited. The frame function `FUN_00401d70` therefore never returned, so nothing presented and nothing drew, while the guest kept executing ~12M boundary crossings a second.

326 instructions across 12 modules were mistranslated (325 `FILD qword`, 1 `FISTP qword`).

## Fix

* `RDI16` / `RDI64` added to `src/recomp/x86rt.h`; `x87_to_int` re-expressed on a new `x87_to_i64` so `FISTP qword` can store all 64 bits.
* `recomp.py` picks the width from the operand and raises `Unsupported` for anything that is not 2, 4 or 8 bytes -- the translator's own rule, which this code was quietly breaking.
* `tests/test_recomp.py::IntegerX87Widths` -- six cases, including the negative (an unknown width must be marked untranslatable BY NAME, not narrowed).
* Every module re-emitted (`XMen2` with its `--isolate` list, which the override machinery needs).

**After the fix the run goes past the limiter into scene traversal** -- `igGraphPath`, `igCamera::activate`, `igTObjectList` -- and stops on an ordinary discovery-loop input: an indirect dispatch to `XMen2.exe 0x00589090` with no recompiled body.

## What the symptom cost, and the two instruments built because of it

The first reading of this was **wrong**, and it was wrong in the way the project's own rules warn about: the ring's last 400 entries were all timer accessors, which was read as "a busy-wait polling the clock, not a frame loop doing work". It was in fact a frame loop that had been doing work at 60fps for hundreds of frames and then stopped. Nothing available at the time could tell those apart, because everything reported only at the END of a run and this run did not end.

**A liveness heartbeat** (`src/native/heartbeat.c`, `X2_HEARTBEAT=<seconds>`, default 5). Its own thread, so a guest stuck anywhere still gets a line, and every zero delta is stated in words:

    [HB]   25.0s  crossings 78453349 (+14309964)  scenes 1056 (+299) ... presents 1056 (+300)
    [HB]   40.0s  crossings 124041117 (+15430303)  ... presents 1358 (+0)
    [HB]           ... and NO frame was presented in that time -- the guest is running, but not reaching Present.

That one line separates "alive and drawing", "alive and not drawing", and "executing nothing" -- three states that had all looked like silence. On a stall it also dumps the boundary ring by itself, which the kill path could not do reliably (a signal handler cannot use stdio; see issue #34).

**The ring records the caller.** The ring only sees boundary crossings, so a loop whose body never crosses one is invisible: it could say "something calls the frame timer forever" and nothing more. Every body entry now also records `_retaddr`, which the generated prologue already had, and the dump prints it:

    [TRACE]   enter  ...  XMen2.exe!0x0055b470 FUN_0055b470  <- 0x00401ffd in XMen2.exe

`0x00401ffd` is the answer to the whole issue, and it took one run.

Also fixed alongside: `X2_ARGS` now caps by NOVELTY (every distinct call site printed once, repeats capped by `X2_ARGS_MAX`) and reports per-entry-point call-site counts at exit, and its exit line prints EDX as well as EAX -- a watch that printed only the low half would have reported this very defect as a plausible small number.

## Ruled out along the way (each with evidence, none of them the cause)

* **A frozen clock.** `QueryPerformanceCounter` is `CLOCK_MONOTONIC` in ns and advances; `QueryPerformanceFrequency` agrees at 1e9.
* **A blocking handle wait.** `WaitForSingleObject` aborts by name and never appears.
* **Counter magnitude overflowing the engine's arithmetic.** The raw ns value is two orders of magnitude below INT32_MAX *for the elapsed value the engine uses*, and the conversion goes through long double. (The overflow was real, but at 2^31 in the TRANSLATION, not in the engine.)
* **Our `_ftol`.** It returns the full 64 bits in EDX:EAX; measured with the argument watch: `getTimeAsLong` returns -2541254811 then 3464323991, i.e. a correct advancing int64.
