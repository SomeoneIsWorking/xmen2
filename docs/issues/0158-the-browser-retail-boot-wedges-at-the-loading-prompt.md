# 0158 — the browser's retail boot wedges at the "Loading..." prompt

- **State items:** S021
- **Status:** reproduced and localized to guest thread suspension; cause not yet found
- **Not** #157: the same source passes the same route on the desktop, and the
  browser's Dead Zone route runs to 1,200 presents on the same build.

## What happens

Start the packaged browser product from its saved installation — the route a
player takes, "Play saved installation", not the `X2_BOOT_MAP` gameplay test —
and it boots, draws the retail splash, reaches the loading prompt:

```
PROMPT DRAW: string at guest 0x700ff388 wchars: ... = "Loading..."
```

and stops there. Ten presents, then nothing, for as long as it is left running
(measured: eight minutes). No memory fault, no refusal, no abort anywhere in the
log.

## What the instruments say

The heartbeat, every 5 s interval, unchanging:

```
tid 1000 start 0x25002590: in a WAIT (condition variable) for 0.0s
tid 1001 start 0x25002600: in a WAIT (condition variable) for 0.0s
tid 1002 start 0x25002630: SUSPENDED for 0.0s
MAIN  tid 999 start 0x00000000: running guest code for 0.0s
KERNEL32.dll!WaitForSingleObject: 4838.2 ms in 602 call(s)
KERNEL32.dll!SuspendThread:        100.4 ms in 301 call(s)
KERNEL32.dll!PulseEvent:             1.3 ms in 301 call(s)
KERNEL32.dll!LeaveCriticalSection:   0.9 ms in 301 call(s)
KERNEL32.dll!QueryPerformanceCounter: 0.6 ms in 301 call(s)
wall-time split this interval: host imports 4927.2 ms (99%), guest bodies 62.3 ms (1%)
```

301 of everything per 5 s is 60 Hz: a multimedia timer cycling, suspending and
resuming tid 1002, pulsing an event, and waiting. Two threads' worth of
`WaitForSingleObject` at about 8 ms a call. The guest executes, but only 1% of
the interval is spent in guest bodies.

`tools/web_profile.py` adds the part that matters: **five of the sixteen targets
never answer the profiler at all** — "blocked, not sampled" — and every target
that does answer is 100% idle. A worker that cannot answer CDP is inside a
blocking wait with no Asyncify unwind, so its event loop never runs. The threads
doing the work are the ones that cannot be sampled. That is a wait, not a spin.

## What it is not

- **Not the guest memory window (#157).** `tools/live_case.py cutscene-skip`
  boots through the retail flow, loads a map and runs a cutscene: 11/11 on the
  ordinary desktop binary and 11/11 on `build/native-window/x2native`, built with
  `-DX2_GUEST_ARENA_WINDOW=1`, which is the same owner and the same layout the
  browser uses. The load path works under the window owner.
- **Not the browser generally.** The same build's Dead Zone route reaches 1,200
  presents in 316 s.
- **Not a memory fault.** Nothing in the run reports one, and the emitted
  permission guard exits through `kX86pJitExitMemoryFault`, which the title logs.

The difference between the two browser routes is that `X2_BOOT_MAP` replaces the
intro, and the retail route runs it — which is where guest threads 1000, 1001 and
1002 come from. So the suspect is `SuspendThread`/`ResumeThread` against a guest
thread under Emscripten, not guest memory and not the renderer.

## What has not been done

Whether this wedge predates the window owner is **unmeasured**. Building the
previous revision needs its own pinned `web-port` dependency prefix, and the
prefix in the shared checkout is newer than that revision accepts. It is recorded
here as unknown rather than assumed either way.

## What would falsify the localization

If a run with the intro's guest threads never created but the retail route
otherwise intact still wedges, the suspicion of `SuspendThread` is wrong.
