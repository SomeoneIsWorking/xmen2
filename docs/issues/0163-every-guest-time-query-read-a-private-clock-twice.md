# 0163 — every guest time query read a private clock, twice

- **State items:** S021
- **Status:** fixed; browser effect not yet re-measured
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

## What is not yet known

The browser effect. The profile share this came from was measured before the
fix and the fix has not yet been re-measured on the route, so no frame number
is claimed here. If `_emscripten_get_now` does not fall, the second reading
was not the cost and the remaining share belongs to the guest's call rate,
not to the pump.
