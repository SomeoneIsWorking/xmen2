---
id: 57
title: The movie rendezvous deadlocks about one run in six: the decoder is suspended and the only thread that resumes it is waiting
status: open
symptom: kernel32: WaitForSingleObject(INFINITE) on event (unnamed) has waited 30 seconds; the guest executed NOTHING in the last 5.0s; one libCriMovie thread SUSPENDED NOW
tags: threads,movie,deadlock,intermittent,pc,native
created: 2026-08-12
updated: 2026-08-12
---

## Symptom

Intermittent, roughly one run in six at 150 seconds (0/5 at 100s, 1/3 at
150s, 2/2 on two 300s runs). The run stops during the intro movies:

    kernel32: WaitForSingleObject(INFINITE) on event "(unnamed)" has waited 30 seconds
      threads: 9 created, 6 exited, 6 reaped, 3 still running; 1035 suspend(s), 18001223 resume(s)
             tid 1008  start 0x25002630  1 suspend(s) 1 resume(s)  SUSPENDED NOW
    [HB] the guest executed NOTHING in the last 5.0s (crossings unchanged)

## What is established

* The stalled state is: the MAIN thread blocked in `WaitForSingleObject`
  (INFINITE) on an unnamed event, the libCriMovie decoder thread
  (start 0x25002630) SUSPENDED, and the other two movie threads blocked. No
  guest code runs at all.
* In a HEALTHY run the same decoder is resumed constantly:
  **54,000,534 of 54,003,175 ResumeThreads are no-ops on a thread that was
  not suspended** -- the main thread spins on ResumeThread while it waits for
  the decoder. That is the game's normal pattern, now counted.
* So the deadlock is an ORDERING one: the decoder self-suspends at a moment
  when the main thread has gone into the blocking wait instead of the spin,
  and the only code that would resume it is that same blocked thread.
* Not a lost handle: `g_resume_unknown` is 0, so every one of those resumes
  named a live thread. This is NOT a recurrence of issue #50.
* `PulseEvent on "(unnamed)" with NOBODY waiting -- the pulse is lost` is
  reported in every run, healthy or stalled. A pulse that arrives while the
  main thread is between "decided to wait" and "registered as a waiter" is
  lost, and under this host's ONE-GUEST-THREAD-AT-A-TIME lock the interleaving
  is coarser than Windows', so the window is bigger here than it is there.

## What has NOT been established

Which event it is, who signals it, and whether the game's own protocol
tolerates a lost pulse on Windows. Naming the event (its handle, its creator,
whether it has ever been signalled) is the next measurement, and reading
libCriMovie's rendezvous is the one after that.

## Do not

Do not "fix" this by making PulseEvent hold a wakeup for a future waiter.
That changes documented semantics to paper over an ordering this host
created, and it would hide the real question, which is whether the coarse
global lock is what makes the window wide.

### Note (2026-08-12)
DEAD END, measured: making ResumeThread a HAND-OFF (broadcast, then guest_cond_wait_ms(1) so the woken thread can take the global lock and actually run) makes it WORSE, not better. With it, the story cutscene stopped presenting entirely after ~180s while boundary crossings went from ~100M per 45s to 4.3 BILLION per 45s -- a livelock: the lock is handed to a thread that also spins without blocking, and the main thread never gets it back. Reverted. What that measurement says about the design: BOTH sides of this rendezvous spin without blocking, so no amount of hand-off-at-a-syscall fixes it -- a one-guest-thread-at-a-time model cannot schedule two spinners. The candidate design is preemption by QUANTUM (release the lock every N boundary crossings) rather than at named syscalls, and that is a real change to the threading model, not a tweak. The starvation is also what makes the cutscene play at 1.7 frames a second (654 real resumes over ~380 seconds = one decoder slice per 590ms).

### Quantum preemption, implemented and MEASURED -- but NOT shown to fix this
The candidate design named above now exists: `guest_quantum()` in
`src/native/threads.c`, called from the dispatch boundary in
`x86_native_call_at` every `X2_QUANTUM` crossings (default 20,000; `X2_QUANTUM=0`
disables it and is the control). If another guest thread is blocked on the
global lock, the running thread drops it, calls `sched_yield()` and takes it
back. One guest thread still executes at a time, so nothing built on that
invariant changes -- only *which* one, and how often that can change.

It fires: 216 preemptions in the first 90 s of a run, 3,413 over a 300 s
gameplay run. The gameplay path is unaffected (no abort, presents keep
climbing, ~1,000 per 60 s).

**It is NOT established that this fixes the deadlock, and nobody should record
that it does without a run sample.** Two measurements argue for caution:

* The lock is contended only **19 times in 140 s**. A thread parked in
  `pthread_mutex_lock` counts as one contention and then sits there, so that
  number does not by itself mean "no thread is ever waiting" -- but combined
  with the preemption count (3,413 fired out of roughly 33,000 quantum
  expirations, so `g_waiters > 0` about 10% of the time) it says most of the
  time the other guest threads are NOT blocked on the lock at all. They are in
  `guest_cond_wait`, waiting to be signalled. A thread waiting for a signal is
  not helped by a preemption.
* This issue is intermittent at about 1 run in 6. Distinguishing that from zero
  needs a sample of runs, not one run that happened to survive.

So the honest state: the mechanism the earlier analysis asked for now exists and
is instrumented, and the premise it was based on ("both sides spin without
blocking, neither ever reaches a release point") is only partly borne out. The
next step is a run sample with `X2_QUANTUM=0` against the default -- and, before
that, finding out what the OTHER thread is actually waiting for, because
`guest_cond_wait` is where the evidence points and a lost `PulseEvent` (already
reported by this run, "with NOBODY waiting") is a better suspect than the
scheduler.

### The lost PulseEvent is NOT the mechanism either -- measured
The note above named a lost `PulseEvent` as the better suspect. It is now
counted rather than suspected: `kernel32_pulse_counts()` totals pulses sent and
pulses lost, and the heartbeat prints both every interval, so the rate is
visible over time instead of as a single "reported once" line.

Over a 130 s run through the intro movies: **3,489 pulses sent, 19 lost with no
waiter -- 0.5%.** Losses appear in the movie phase and stop with it (the count
is flat after 90 s, when the movies end). Half a percent cannot starve a
handshake that is being pulsed roughly 47 times a second, so this suspect is
ruled out as the systemic cause the same way the quantum was.

What the same run shows that is worth the next session's attention: the movie
phase presents ~21 frames a second with one draw in each, not the 1.7 frames a
second recorded higher up this file. Either the starvation is intermittent in
the same way the deadlock is, or something committed since that measurement
changed it. Re-measure before treating either number as this issue's baseline.

Ruled out so far, each with evidence rather than reasoning: a hand-off at
ResumeThread (made it worse -- livelock), preemption by quantum (fires, changes
nothing observable), lost pulses (0.5%). What has NOT been looked at is what the
decoder thread is actually blocked ON at the moment of a stall -- there is no
per-thread state report, and every attempt so far has guessed at the mechanism
instead of reading it. That instrument is the next thing to build.

### The instrument now exists: what each thread is blocked ON, live
`guest_thread_state_report()` prints one line per live guest thread from the
HEARTBEAT -- state (running / waiting for the guest lock / in a condition wait /
in a blocking host call / SUSPENDED / new) and how long it has been in it.
A stall has to be watched while it happens; a shutdown report arrives after the
SIGKILL that ended the argument, which is how the first quantum measurement was
lost.

First reading, during the intro movies: each movie runs THREE guest threads --
`0x25002590`, `0x25002600` and `0x25002630`. Two sit in a condition wait and one
runs guest code, and on the next movie the third is SUSPENDED. The durations
read ~0.0s every interval, which is the useful part: those two are not parked,
they keep WAKING. This is a poll loop between three threads, not a two-way
rendezvous, which is what every attempt on this issue so far has assumed.

A separate thread, start `0x2f075da0`, was caught "waiting for the guest lock
for 0.4s" -- so a thread genuinely does block on the lock, which is what the
quantum acts on.

The instrument had a defect of its own on its first run and it is fixed: a
thread that had never changed state reported its age as the process uptime,
8,989s on a 130s run. Stamped at creation now.

### A run sample, at last -- 6 of 6, which is NOT evidence the deadlock is gone
This issue has never had a sample, because a run could not be repeated: the
scripted input fired at fixed TIMES and the same script on the same machine
reached the same frame anywhere between 106s and 140s. With `X2_INPUT_SCRIPT`
scheduling on frames PRESENTED, a run is repeatable, and six identical runs
were made (`scratch/logs/sample1..6.log`, driven by the script now in
`tools/smoke_loop.sh`).

**All six closed the loop**: every one of the six scripted key presses fired,
including the last, which is only reachable through the intro movies, a level
load, gameplay and the death dialog. No run reported a stall (the heartbeat's
"EXECUTING but has presented nothing" fired zero times in all six). Completion
spread 261s to 310s.

**What that is worth, stated honestly: very little on its own.** If the rate
really is one run in six, the chance of seeing no failure in six runs is
(5/6)^6 = 34%. This sample cannot distinguish "fixed" from "unchanged" and must
not be recorded as either. It is a BASELINE: the first repeatable measurement
this issue has, and the method by which a real one can now be taken. Roughly 25
runs would put a one-in-six rate below 1% of going unseen.

The movie phase IS exercised by these runs -- the first scripted press is at
frame 2639, after the intro movies have played.

### 22 runs, 0 failures -- the one-in-six rate is rejected at 95%
Sixteen more runs of `tools/smoke_loop.sh` (on top of the six above): **all 22
closed the loop, none stalled.**

The arithmetic, since "22/22" invites over-reading. If the rate really were one
in six, the chance of seeing no failure in 22 runs is (5/6)^22 = 1.9%, so the
data is inconsistent with p = 1/6 at the 95% level. The one-sided 95% upper
bound from 0 failures in 22 is p < 1 - 0.05^(1/22) = 12.7%. So: the rate as
recorded at the top of this file is rejected, and anything up to about one run
in eight is still consistent with what has been seen. **This issue is not
closed.**

Two limitations that matter more than the arithmetic:

* Every run in the sample SKIPS the story cutscene, with Escape about twenty
  frames after it starts. The intro movies do play in full before the first
  scripted press, so the movie path is exercised -- but the story cutscene
  barely is, and that is where the rendezvous was first seen.
* Nothing attributes the improvement. Between the original observation and this
  sample the port gained quantum preemption (which fires), the mid-frame clear
  reopen, SSE, and a repaired truncated body. Any of them, or none of them,
  could be responsible.

The experiment that would attribute it is cheap now: `X2_QUANTUM=0` is the
control, and `X2_MAX_FRAMES` cuts a run from 6:55 to 3:40, so a 16-run control
sample is about an hour. Run that before crediting the quantum.


### CORRECTION: the control runs were issue #54, not this one
The `X2_QUANTUM=0` control above came back 0 of 16 and was written up as
attributing something to preemption. It attributed nothing to this issue. Those
runs were failing with "no device was ever created" -- issue #54 -- and #54's
cause has since been found by reading the exe: the DirectX-check override
returned no value and the game does `TEST AL,AL` on it, so it branched on
leftover EAX. Preemption changed what ran before the call, which is why turning
it off changed the outcome; it was never scheduling.

With that fixed, `X2_QUANTUM=0` passes 4 of 4 given a timeout that fits (a run
without preemption is about twice as slow, which is a real effect and the only
one attributable to the quantum so far).

So this issue's sample is unchanged in what it says: 22 runs, no failures, the
one-in-six rate rejected at 95%, cause unattributed, still open. The lesson is
the method -- the empirical path here produced two wrong attributions in a row,
and reading the guest's own code produced the answer in twenty minutes.

### The rendezvous, read out of the guest (RE, not sampling)
`libCriMovie`'s IAT resolves at `0x1004204c` WaitForSingleObject, `0x10042050`
PulseEvent, `0x10042054` CreateEventA, `0x10042058` SuspendThread, `0x1004206c`
ResumeThread, `0x10042070` SetThreadPriority, `0x100420d0` timeSetEvent. Two
functions are the whole protocol:

* **`FUN_10002630` -- the decoder thread** (the logs' `start 0x25002630`). Loop:
  ask `FUN_10008600` whether there is work; if there is *and* the flag at
  `[0x100572b0]` is not 1, jump straight back to the top and keep working. Only
  when it is out of work does it fall through, clear `[0x100572b0]`, restore its
  priority and call `SuspendThread` **on its own handle** (`[0x1014a1fc]`) at
  `0x100026b0`. It parks itself; it does not park between slices.
* **`FUN_10002520` -- its partner.** Sets `[0x100572b0] = 1`, then SPINS up to
  `0x2dc6c0` (3,000,000) iterations calling `SetThreadPriority` +
  `ResumeThread` on `[0x1014a1fc]` until the decoder clears the flag, and calls
  an error routine if it runs out.

`FUN_10002bb0` is the stop side: set the stop flag `[0x100572d4]`, raise the
decoder's priority, `ResumeThread`, then `WaitForSingleObject(handle, 1000)` in
a retry loop on `WAIT_TIMEOUT`.

**Both sides spin without ever blocking.** That is the fact that decides the
design, and it is why every hand-off attempt has failed.

### DEAD END, measured (second attempt): a hand-off that WAITS for the resumed thread to park
Since the first hand-off failed by yielding blindly, the obvious repair was to
make it deterministic: on a `ResumeThread` that actually released a suspended
thread -- 3,571 of them in a run, not the 63,000,000 no-ops -- run that thread
**to its next park** before returning, i.e. resume it the way a coroutine is
resumed. Bounded at 2 s, counted, reported with its denominator.

It froze the run harder than the first attempt did: presents stopped dead at
frame 2639 and boundary crossings went to **3.17 billion in sixty seconds**
(against ~124 million in the first minute of a healthy run). The reason is in
the RE above -- the decoder does **not** park while it has work, so "wait for it
to park" waits for something that only happens when the decoder runs dry, and
the thread that would give it work is the one waiting. Reverted.

What the two measurements together establish: **no hand-off at a syscall can fix
this, because neither side ever blocks.** It is a scheduling problem, and the
only mechanism that schedules two spinners is preemption neither side has to
cooperate with -- `guest_quantum()`.

### A real defect that preemption itself introduced: critical sections
`EnterCriticalSection` was a depth counter, documented as safe because "nothing
here creates a guest thread". By the time the quantum landed, the game was
creating nine, and a thread could be preempted *inside* a section with another
walking straight in. Fixed in `src/native/kernel32.c`: real ownership on Win32's
own `RTL_CRITICAL_SECTION` layout (owner = the **guest** thread id, recursion
count, waiters), waiting by condition wait rather than by spinning, and
`GetCurrentThreadId` now returns the guest id so the two agree. Leaving a
section owned by another thread aborts by name instead of corrupting it.
