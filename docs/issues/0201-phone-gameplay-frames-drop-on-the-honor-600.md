---
id: 201
title: Phone gameplay drops frames on the HONOR 600
status: open
symptom: On the HONOR 600 (VKJ-NX9) the player feels frame drops in ordinary gameplay, on the act 2 jungle path with a party of four, a ground fire and foliage
tags: android,arm64,performance,x87,jit
created: 2026-10-29
updated: 2026-10-29
---

# 0201 — Phone gameplay drops frames on the HONOR 600

## How it is measured

The phone refuses `perf_event_open` to shell and app alike, although
`perf_event_paranoid` is -1, so simpleperf cannot run there. The profile comes
from the in-process host sampler instead (`GET /performance/sample`,
codemap "In-process host sampler"). Each sample is one 250 Hz kernel tick of
frame-thread CPU, about 4 ms. `tools/jit_map_profile.py --samples` charges each
sample to a guest block through `jit.map`, or to a host symbol through the
unstripped `libmain.so` and the device's `libc.so`.

Displayed cadence comes from `dumpsys SurfaceFlinger --latency` on the game's
layer. The port's `frame_ms` is a CPU timestamp taken after submit, not what
reaches the screen.

Do not arm `hotep` or `jit.profile` for these runs. Both turn chaining off. An
early cave run with `hotep` showed 6.2% doubled frames; the same spot without
it showed 0.2-0.6%.

## Baseline (44b85c3, the user's save in the cave, Continue)

Standing, then walking a fixed 12-key pattern for 38 s:

| measure | value |
|---|---|
| CPU `frame_ms` | p50 16.67, p95 18.49, p99 19.81, max 88.2 ms |
| displayed frames | 0.6% held for 2 vsyncs, one 83 ms hitch |
| standing at the jungle1 start | no dropped frames |

The frame thread is about 87% busy: 14.6 ms of CPU per 16.7 ms frame. A walk
that loads more or draws more crosses the vsync.

By family, 8,476 samples:

| family | share | per frame |
|---|---|---|
| JIT guest code, flat over 1,944 blocks | 57.4% | 8.4 ms |
| driver and SDL | 14.3% | 2.1 ms |
| engine crossing and dispatch | 4.4% | |
| x87 soft helpers | 4.4% | |
| libc mem and alloc | 3.8% | |
| d3d8 translation | 3.4% | |
| scheduler and clock | 3.1% | |

The heaviest leaves:

| leaf | share |
|---|---|
| `__ioctl` | 3.49% |
| igTraversal::dispatch | 1.95% |
| XMen2.exe `0x5fcf70` | 1.73% |
| vdso `clock_gettime` | 1.72% |
| memmove | 1.51% |

Of the `__ioctl` share, 2.08% is the upload submit under
`gpu_upload_batch_flush`. Of the clock share, 1.38% is
`guest_thread_any_ready` reading CLOCK_MONOTONIC on every quantum.

The vec3 helpers at XMen2.exe `0x5fcf70..0x5fe7e4` total about 9.4%. Their six
small leaves are 3.76% (transform 1.73, cross 0.88, add 0.42, scale 0.27,
dot 0.26, sub 0.20).

On arm64 each x87 instruction becomes about 43 host instructions. The exact
ext80 slot is located, its tag checked, the value narrowed to binary64 and
widened back, with a helper fallback beside it. `0x5fcf70`'s 33 x86
instructions are 5.7 KB of arm64. This is the systemic lever. It lives in
x86port's arm64 x87 emitter and is not addressed here.

## Changes

- **vec3 leaves**: XMen2.exe's six helpers are native overrides and JIT leaves
  (codemap "Native vec3 leaves"). They run in the bodies' x87 order, and their
  operand snapshot repeats any overlap exactly. `math.vec3_verify` on the
  desktop (exact x87 host) matched 16.7M native answers against the guest
  body through the boot-continue case.
- **Same-binary desktop A/B**, `math.vec3=0` vs `1`, both paced in the saved
  map. Each call to the affine transform fell from about 110 ns to about 75 ns,
  and each cross from about 120 ns to about 80 ns. The call rate comes from the
  heartbeat's `override leaves completed this interval` line: about 60k
  transforms per second.
- **Scheduler clock**: `guest_thread_any_ready` takes the earliest pending
  condition deadline. It now asks `guest_clock_reached`, which answers "not
  yet" from CLOCK_MONOTONIC_COARSE while the deadline is more than two coarse
  ticks away, and reads the precise clock only near it. On the phone the
  precise clock costs 54 ns per call and the coarse one 13.5 ns, with a coarse
  resolution of 4 ms.

## Open

- The device after-numbers in the same scene. The phone's wireless adb went
  away before the A/B could run on it.
- The upload submit `ioctl` (2.08%). The cause is not yet traced; staging
  cache maintenance is the suspect.
- Every frame makes three queue submits: uploads, the shadow pass and the
  frame.
- The larger x87 functions at `0x5fd7e0` and `0x5fd980` are not overridden.
