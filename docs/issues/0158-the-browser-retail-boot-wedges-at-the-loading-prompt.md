---
id: 158
title: the browser's retail boot wedges at the "Loading..." prompt
status: investigating
symptom: the retail #play route never leaves Loading...; the guest allocator fails and the title's own fatal handler hangs on a JMP $ at 0x403210
state_items: S021
tags: web,browser,wasm,boot,threads,wedge
created: 2026-09-19
updated: 2026-09-19
---

# 0158 — the browser's retail boot wedges at the "Loading..." prompt

- **State items:** S021
- **Status:** cause found. The spin at `0x403210` is the tail of the title's
  own fatal allocation-failure handler, called from
  `libIGCore.dll!igMemoryPool::allocationFailure`. The guest allocator failed
  and the title hung on purpose. What is not yet known is the reason code and
  the size, because the port never prints the title's own message. This
  supersedes the localization to thread suspension and the `Present` reading
  below.
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

### The last import the main thread crossed was `Present`

Answered 2026-09-19. The boundary ring could not say it: the ring is one
shared record and on this route it is filled by the 60 Hz timer thread, 3,582
crossings per five seconds, so the wedged thread's last act was long gone from
it. The crossing is now stamped into the crossing thread's own record and the
heartbeat prints it. On the retail `#play` route:

```
tid 1000 ... in a WAIT (condition variable) for 0.0s
  last crossed into ResumeThread at guest 0x000c0fd0, 0.0s ago
tid 1001 ... in a WAIT (condition variable) for 0.0s
  last crossed into WaitForSingleObject at guest 0x000c0f80, 0.0s ago
tid 1002 ... SUSPENDED for 0.0s
  last crossed into SuspendThread at guest 0x000c0fb0, 0.0s ago
MAIN tid 999 start 0x00000000: running guest code for 0.0s
  last crossed into Present at guest 0x000c19a0, 398.9s ago
```

The three auxiliary threads keep crossing every beat; the main thread crossed
into `Present` once and never crossed again, for the whole 399-second run.

**`Present` is not a noreturn.** So the first branch of the dichotomy above is
closed: no import of ours returned from something the title declared it would
not return from. What remains is one of the two conditional branches -- the
null pointer at `[esp+0x18]` or the reference count that did not reach zero --
or the `call edi` fall-through with EDI pointing at guest code rather than an
import.

### The cause: the guest reports an allocation failure and hangs on purpose

Answered 2026-09-19 with `--set jit.watch`, in two runs.

**Which branch.** Watching `0x00403210` (`scratch/web/wasmgoal/watch158`):

```
jit.watch: entry 1 to guest 0x00403210 (unnamed)
jit.watch:   came from block 0x004031e7 (unnamed)
jit.watch:   eax=0467200c ecx=04672008 edx=0c00004c ebx=0c00004c
jit.watch:   esp=700ff560 ebp=0c00004c esi=0c00004c edi=2f045ff0
jit.watch:   this thread last crossed into Present (thunk 0x000c19a0), 0.002s ago
```

`ECX` is `EAX - 4`, which is exactly what `4031f1: mov ecx,eax` followed by
`4031f8: add eax,0x4` produces, so the run went *through* the release body:
the `je` at `4031f3` was not taken, and the `jne` at `403206` was. The
reference count is a bitfield (`test edx,0x7fffff`) and it stood at
`0x0c00004c` after the decrement, so it did not reach zero and the call at
`403208` was skipped into the guard.

**Which function.** The disassembly above is the tail of the function entered
at `0x00402cf0`, whose body is a message: it loads
`libIGCore.dll!igOutput::toStandardOut` from the IAT at `0x0067f720` and
prints

```
\nAllocation failure:\n
    Reason          = %s\n      (or = %d for an out-of-range code)
```

selecting the reason from the five-entry table at `0x006d4ba0`:
`kAllocationFailureUnknown`, `kAllocationFailureMaxSizeExceeded`,
`kAllocationFailurePoolExhausted`,
`kAllocationFailureSystemMemoryExhausted`,
`kAllocationFailureMemoryOperationInhibited`. It ends in `JMP $` by design:
this is the engine's fatal handler and it is meant to stop there.

**Who calls it.** Watching `0x00402cf0`
(`scratch/web/wasmgoal/watchfatal`):

```
jit.watch: entry 1 to guest 0x00402cf0 (unnamed)
jit.watch:   came from block 0x2f03ab30
             (?allocationFailure@igMemoryPool@Core@Gap@@MAE_NIW4igAllocationFailureReason@23@@Z)
jit.watch:   this thread last crossed into WaitForMultipleObjects, 0.002s ago
```

**So the browser boot is not wedged by a port defect in threads, suspension,
rendering or the guest memory window. The guest allocator failed, the title
reported it, and the title hung, exactly as it was written to.** The run then
produces the two `d3d8: LockRect on the back buffer ... refusing` lines at
17:19:16.651 and .655 -- *after* `allocationFailure` at .632, so they are a
consequence and not the cause.

This makes #158 a memory-size issue and links it to #159, which measured the
browser committing 2.5 GB before it can draw: the same run has both a large
flat guest window and a pool that will not grow.

### What to do next with it

1. **Name the reason and the size.** `igMemoryPool::allocationFailure` takes
   the failed size and the reason code; both are on the guest stack at its
   entry and neither is in any log, because the title's own message goes
   through `igOutput::toStandardOut` and **this port never prints it** -- no
   untagged guest output appears anywhere in the run. The game is saying
   exactly what went wrong and the port is throwing it away. That is a
   diagnostic defect of its own and it is the cheapest next step.
2. Then size the pool. `kAllocationFailurePoolExhausted` and
   `kAllocationFailureSystemMemoryExhausted` point at different owners.

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
