# 0158 — the browser's retail boot wedges at the "Loading..." prompt

- **State items:** S021
- **Status:** the wedged thread is located exactly — it is spinning on a `JMP $`
  the title itself contains, at guest `0x403210`. What sent it there is not yet
  known. This supersedes the earlier localization to thread suspension; see
  "Where the wedged thread actually is".
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

## Where the wedged thread actually is

**One guest block is 83.6% of every block the run enters, and it is `JMP $`.**
Found by accident: a new per-entry counter in x86port (`blocks_reentered`, the
times the block entered was the one just left) read **84.3% cumulative and 99.8%
over the last window** on this route. A number that extreme is not a property of
gameplay, and the hot-block histogram named the block:

```
1. 0x00403210 unnamed        348021750  83.6%
2. 0x2f03afb0 ?getMemoryPoolByIndex@igMemoryPool@Core@Gap@@KAPAV123@H@Z  1225835  0.3%
```

The bytes at that address, read from the player's own `XMen2.exe`, are `EB FE`.
The function ends there, and **every path through its tail arrives at it**:

```
4031e1:  call DWORD PTR [edx+0x2bc]
4031e7:  mov  eax,DWORD PTR [esp+0x18]
4031eb:  test eax,eax
4031f3:  je   0x403210            <-- hang if that pointer is null
4031f5:  mov  esi,DWORD PTR [eax+0x4]
4031fb:  dec  esi                 ; a reference count
4031fe:  test edx,0x7fffff
403204:  mov  DWORD PTR [eax],esi
403206:  jne  0x403210            <-- hang if it did not reach zero
403208:  call edi
40320a:  lea  ebx,[ebx+0x0]       ; padding
403210:  jmp  0x403210            <-- and the fall-through from that call
403212:  int3 int3 int3 int3      ; end of function
```

A `jmp $` immediately after a call, with `int3` padding behind it, is what a
compiler emits behind a **noreturn** call: the guard for a function that must
never come back. So there are two candidate causes and this does not yet say
which — an import of ours RETURNING from something the title declared noreturn,
or one of the two branches above taken on a null pointer or a refcount that did
not reach zero.

**This corrects "that is a wait, not a spin" above, for the main thread.** Both
readings are right about different threads: the auxiliary threads are in waits,
and the run keeps crossing to imports 3,582 times per 5 s from them, which is
what the 60 Hz timer table below shows. The MAIN thread is not waiting at all —
it is executing `EB FE` about twenty million times a second. The profiler could
not see this because **a worker spinning in guest code never answers CDP**, so
"blocked, not sampled" was read as a blocking wait when for this thread it was a
busy loop. An idle profile and a hard spin look identical from outside.

### What to do next with it

`call edi` is the lead: EDI's value is not known statically, so log the import
the main thread last crossed into before the block at `0x403210` is first
entered. If it names a noreturn (`ExitProcess`, `ExitThread`, `abort`,
`_purecall`), the fix is that import, and the guest is behaving correctly. If no
import was called, one of the two conditional branches took it and the null
pointer or the reference count is the thing to find.

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
