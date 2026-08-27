# In-game cutscene player

Gameplay cutscenes in the shipped title are not owned by the conversation
manager. They are control-lock epochs composed from BehavEd fibers, the
title's timed entity-event player, and deterministic conversation payloads.
The native owner is `src/native/cutscene_player.c`; its
`cutscene_dialogue.c` component owns dialogue suppression while conversation
code remains a payload adapter only.

## Authored boundary

`lockControls` reaches `XMen2.exe` `00469130`. A negative duration writes `-1`
to the control deadline at clock owner `+0x3f4`; a positive duration writes
`now(+0x3e8) + duration`. The tutorial begins with `lockControls(-1)` and its
final cleanup uses `lockControls(0.1)`. No callback performs the final release.
During synchronous completion the player consumes that final finite delay on
its private cutscene timeline by making the deadline current, without changing
the guest clock or running a world frame.

The current BehavEd context is published at `00787730`. Runner `004d8b30`
writes it before invoking a command and restores the prior value only after the
command returns. That is the causal identity used for work posted by the
active cutscene. Context allocation inherits only from that owned parent, an
owned timed-event callback, or a deterministic conversation payload; the
control-lock epoch alone is not ownership.

## BehavEd player

`CPythonGameInterface::update` (`004a00d0`) reaches timed fiber pump `004d9640`;
`004d8b30` executes a selected context. The scheduler lives at manager
`+0x3a080`, with 30 context slots and a `{float deadline, context-slot}`
min-heap. Ordinary execution retains retail's strict `deadline < now` rule.
The exact-step seam in `src/native/behaved_player.c` removes and resumes only
an owned context, independent of its deadline, while preserving foreign heap
pairs.

`waittimed` (`004d9130`) only suspends the current fiber. It is not overridden,
and the global scheduler insertion/deadlines are never clamped.

## Timed entity-event player

The missing `nightcrawler_spawn -> nightcrawler_walk` transition is an engine
event, not a conversation or animation completion:

1. `act` (`004a9010`) reaches spawner handler `0048b000`.
2. `0048a580` posts a zero-delay record through `0041a730` and `004b2b40`.
3. On a later retail frame, pump `004b2d70` removes the record, dispatches it
   through `004199f0`, then frees its slot through `004b2ea0`.
4. Callback `0048a7d0` calls `0048a5a0`, which instantiates the actor and
   launches its `firstspawnscript` (`nightcrawler_walk`) at `0048a779`.

The scheduler owner is the static subobject at `0075d344`. It has `0x465`
(1,125) 24-byte callback records, allocation bitsets at `+0x6978` and
`+0x7bac`, a live count at `+0x7c3c`, and a min-heap at `+0x7c40` whose count is
at `+0x9f6c`. `004b2b40` is a void `thiscall(owner, float deadline,
record24*)`; EAX does not identify the slot. The port therefore validates and
differences both allocator bitsets around the retained retail body and accepts
exactly one new slot.

Ownership is attached at that insertion seam only when `00787730` names an
owned BehavEd context. A callback-posted child is entity/world work and does
not inherit cutscene ownership. The live falsifier for broader inheritance was
an effect event targeting handle `0x3fd`: it reposted itself 4,093 times and
hit the runaway guard. Script-posted events execute once in the cutscene
player; their recurring descendants remain in the ordinary event player.
An owned event may synchronously allocate a BehavEd context, as the tutorial
spawner does for `nightcrawler_walk`; that direct context inherits through the
owned callback scope without adopting the callback's recurring event children.

During callback dispatch the parent slot has already left the heap but remains
allocated until `004199f0` returns. Validation explicitly admits that one
in-flight slot. Treating it as corruption refused 21 legitimate ordinary
insertions in the first live attempt; `test_cutscene_event_player` now covers
ordinary callback insertion and dispatch-before-free order.

## Animation and conversation payloads

`playanim` (`004a8a20`) synchronously resolves the actor and animation, installs
the graph state through `00430d40`, and returns. The shipped tutorial scripts
use explicit `waittimed` calls for every animation duration; there is no third
animation-completion player to drain.

The conversation adapter calls the same vtable `+0x18` transition as retail
input, but only when exactly one response exists. Choices refuse the skip.
Selecting the final `0020b` response synchronously launches
`conv_0020b_end`; no world update is required between response and cleanup
fiber allocation.

Retail acceptance first stops the active dialogue handle through the audio
manager vtable at `+0x74`, clears conversation accept-state bit zero, and then
calls `chooseResponse` (`0045d5d0`). That transition reaches two distinct
voice presenters:

- `beginResponse` (`00458700`) starts response voice data from record `+0x54`
  and returns true to defer applying the response until a later frame.
- `0045a170` starts a line voice from record `+0x5c`/`+0x54`, stores its handle
  at conversation manager `+0x21b80`, and publishes its duration at `+0x239a8`.

The synchronous player has no presentation frame between deterministic
responses. Re-entering `chooseResponse` therefore started every response voice,
while BehavEd/event work could start the next conversation's initial line
inside the same invocation. `cutscene_dialogue` stops the currently active
handle and scopes both exact presenters across the whole player invocation.
Within that scope `00458700` preserves its unconditional chosen-response write
but reports false so the authored response and scripts apply immediately;
`0045a170` returns before line presentation. Outside the scope both overrides
super-call their retained generated bodies. The player also opens a generic
new-voice suppression scope in `src/audio/audio_play_policy.c`; the native
DirectSound `Play` boundary acknowledges but does not start any buffer reached
while that scope is active. This catches cutscene SFX and other authored audio
without pausing the backend or disturbing already-running ambient/gameplay
voices. Neither layer changes guest/world time, and an end-of-scope dialogue
handle check records any unrecognized presentation path as a leak.

## Verification contract

`tools/live_case.py cutscene-skip --pacing fast` presses Escape on the first
visible tutorial record. It passes 11/11: both transition scripts and cleanup
launch, the adjacent conversation starts, controls return, and one request
completes in one player invocation with frame and guest clock unchanged. The
player stops one active voice, suppresses five response starts and four line
starts, blocks two additional DirectSound starts, and records zero leaked
presentation starts.

`tools/live_case.py cutscene-skip-early --pacing fast` presses during the
camera-only locked stretch before any conversation exists. It passes 10/10,
suppresses five response and five line starts with zero leaks, blocks two
additional DirectSound starts, and
proves the control-lock cutscene player—not the conversation manager—is the
owner. Both runs launch `x2native` with `--no-window --unbounded`, dummy audio,
and unpaced scheduling.

`test_cutscene_dialogue` supplies the instrument's opposite answer: ordinary
calls reach both retained presenter bodies and increment their ordinary-start
counters. Its skip cases then prove current-handle cancellation, response
application, exact presenter suppression, and whole-player suppression for an
adjacent line without entering either retained body.
`test_audio_play_policy` supplies the lower boundary's opposite answer and
proves nested suppression refuses starts while ordinary playback remains
enabled.
