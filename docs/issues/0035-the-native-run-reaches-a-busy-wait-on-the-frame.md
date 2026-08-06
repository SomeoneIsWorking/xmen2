---
id: 35
title: The native run reaches a busy-wait on the frame timer after submitting its first draws
status: open
symptom: x2native --d3d8 --run never returns; the boundary ring's last 400 entries are ONLY igLongTimer::getTimeAsLong / igWin32LongTimer::getTimeOfDay / FUN_0055b470 / FUN_0055b610
tags: pc,native,timing,main-loop,rc-exe
created: 2026-08-07
updated: 2026-08-07
---

## Where the run gets to

Module init, CRT, engine memory, ARK, renderer init with a live Vulkan device, texture/surface resource work, the whole input subsystem -- and then:

    gpu: the depth test is requested but there is no depth target yet, so it is IGNORED.
    kernel32: LoadLibraryA("DSOUND.DLL") -> NULL
    kernel32: GetProcAddress(shell32.dll, "SHGetFolderPathA") -- not implemented, so NULL

**A draw was submitted** (that is what the depth-target line means), sound init was attempted, and the save-folder lookup ran. Then it spins.

## What the spin is

`281,001,389` boundary crossings, and the last 400 ring entries contain FIVE bodies and nothing else:

    XMen2.exe!0x0055b470       FUN_0055b470          (a timer accessor)
    XMen2.exe!0x0055b610       FUN_0055b610          (reached via CALL [EDX+0x60])
    libIGCore!0x1000cf30       igLongTimer::getTimeAsLong
    libIGCore!0x10068fd0       igWin32LongTimer::getTimeOfDay
    libIGCore!0x100763d0       MSVCRT::ftol

No rendering bodies, no traversal, no file I/O. So this is a **busy-wait polling the clock**, not a frame loop doing work.

## Ruled out

* **The clock is frozen.** It is not: `QueryPerformanceCounter` is `CLOCK_MONOTONIC` in nanoseconds and advances; `QueryPerformanceFrequency` agrees at 1e9.
* **A blocking handle wait.** `WaitForSingleObject` aborts by name and does not appear in the log at all.
* **Counter magnitude overflowing the engine's arithmetic.** Host uptime at the time was 3.5 hours: 1.28e13 ns, 1.28e7 ms -- two orders of magnitude below INT32_MAX, and the engine's conversion is `FILD qword` into long double before scaling.

## Where to start

`FUN_0055b470` is reached INDIRECTLY (no direct CALL to it exists in the image), so the spinning caller dispatches through a vtable and the ring cannot name it -- the ring only records boundary crossings, and the loop body itself never crosses. Finding the caller needs either a guest backtrace from the interrupt handler (the return address chain is on the guest stack) or a watchpoint.

Two candidates worth testing first: the frequency this host reports (1e9 is unusual; real hardware is ~1e7, and an engine that captured it into a float scale may compute a wait that is orders of magnitude too long), and something the game is waiting for that needs a THREAD -- this process has one guest thread and `DSOUND.DLL` did not load.
