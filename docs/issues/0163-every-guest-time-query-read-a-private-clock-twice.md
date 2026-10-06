---
id: 163
title: every guest time query read a private clock, twice
status: resolved
symptom: time queries bypassed guest_clock and read the host clock directly, twice per query
state_items: S021
tags: web,browser,wasm,timing,guest-clock
created: 2026-09-19
updated: 2026-09-19
---

# 0163 — every guest time query read a private clock, twice

- **State items:** S021
- **Status:** fixed and re-measured; the performance half is worth about 0.3%
  of the guest worker, the correctness half is the reason it stays
- **Found by:** the #162 profile, where `_emscripten_get_now` was 4.19% of the
  guest worker and `_clock_time_get` a further 0.68%

## The correctness half, which is the serious one

`src/native/guest_clock.h` states its own contract in its first paragraph:

> There were five private copies of `now_s()` reading CLOCK_MONOTONIC
> directly, and that is not a tidiness complaint: the guest drives real logic
> off elapsed time (multimedia timer deadlines, thread waits, DirectSound play
> cursors, QueryPerformanceCounter), so two of those advancing differently is
> a timing bug that presents as a gameplay bug. They now all come from here.

They did not. `guest_clock_ns()` — the function the header names as the one
QueryPerformanceCounter and GetTickCount use — **had no callers at all.** Three
places read `clock_gettime(CLOCK_MONOTONIC)` for themselves:

| site | import |
|---|---|
| `kernel32.c` `imp_KERNEL32_QueryPerformanceCounter` | QueryPerformanceCounter |
| `kernel32.c` `imp_KERNEL32_GetTickCount` | GetTickCount |
| `x86_import_fastpath.c` `import_qpc` | QueryPerformanceCounter, JIT fast path |

The consequence is exactly the one the header predicts. `guest_clock` owns an
idle skew that jumps forward over intervals in which no guest thread could
run, which is what `--unbounded` is. A raw reading does not carry that skew,
so under unbounded the guest's own QueryPerformanceCounter would report less
elapsed time than the port had actually skipped — and the fast path and the
slow path for the *same import* would disagree with each other by that amount.

## The cost half

QueryPerformanceCounter is also the pump point for the multimedia timers,
which have no thread of their own, so any guest loop waiting on one calls it
constantly. `winmm_timers_pump()` then read the clock **again** for itself. Two
clock reads per QPC, and in the browser a clock read is a call out of
WebAssembly into JavaScript.

## The fix

`winmm_timers_pump_at(double now_s)` takes the instant its caller has already
read; `winmm_timers_pump()` remains, reading the clock for callers that have
not. The three sites above call `guest_clock_ns()` once and pass that instant
to the pump, so one reading serves both the answer and the timers.

`guest_clock_ns()` is now computed in integers rather than by scaling the
seconds view. That is one multiply and one divide fewer per call, and nothing
more: **precision is not the reason**, although the first version of this
issue and of the source comment said it was. The claim was that a double
holding the instant could not carry nanoseconds; it holds *seconds* since
boot, around 1e5, where 53 bits resolve far below a nanosecond. The test
written to prove the precision claim passed against both forms, which is how
the mistake was caught, and those checks were deleted rather than kept — a
check that cannot fail is worse than no check.

## What proves it

`tests/test_guest_clock.c` (8 checks). The discriminator is the idle skip,
because it is the only thing a private reading gets wrong: a raw
CLOCK_MONOTONIC is monotonic, agrees with the seconds view well inside a
millisecond, and has nanosecond resolution either way. Making
`guest_clock_ns()` ignore the skew fails the test with "the five-second skip
moved the counter by 110 ns".

`test_x86_import_fastpath` now links the real `guest_clock.c` rather than
stubbing it, so the fast path's QPC answers through the shipping owner.

## The browser effect, measured — and it is small

`_emscripten_get_now` went from **4.19% to 3.88%** of the guest worker on the
same route. It fell, so the second reading was real, but it fell by about a
fourteenth of itself rather than by half.

**So the pump's reading was a small minority of the clock cost, and the answer
to "why is the clock 4% of the worker" is the guest's own call rate.** The
check this issue set itself — "if `_emscripten_get_now` does not fall, the
second reading was not the cost" — came back almost that way, and the honest
reading is that this change is worth a fraction of a percent of frames.

That does not retire the fix, because the reason to make it was never the
0.3%: three call sites were bypassing the clock owner and would have handed
the guest a QueryPerformanceCounter that disagreed with itself under
`--unbounded`. The performance argument was the one that found the defect, not
the one that justifies it.

What is still open is the remaining 3.88%. A guest calling QPC often enough to
spend 4% of a frame on it is a fact about the guest's loop, not about this
port, and the next question is whether that loop is a wait the port could
satisfy without a round trip to JavaScript — not whether the reading can be
made cheaper.

## What would falsify the attribution above

Profile shares moved between builds partly because inlining changed: the
memory-owner fix made `x86p_mem_read_bytes` and `x86p_mem_write_bytes` show up
as named frames where they had been folded into their callers, so summing
"memory path" frames across builds compares different partitions.

Frame rate does not have that problem and has a worse one. This run's
presents/s by age went 6.75, 6.73, 6.35, 6.76, **5.49** — a spread inside one
run three times larger than the difference between the builds being compared,
on a change the profile puts at 0.3%. It is recorded in #162 because it
retires the frame figures quoted there too: **one wall-clock-paced run of this
route cannot resolve a change of this size**, and this issue's own "worth a
fraction of a percent of frames" is a statement about the profile, not
something the frame counter showed.
