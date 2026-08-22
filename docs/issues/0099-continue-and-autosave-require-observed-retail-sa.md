---
id: 99
title: Continue and transactional post-map autosave are implemented
status: resolved
symptom: Retail had no Continue row or host-owned autosave checkpoint, and the first Continue integration scanned the wrong save directory and stopped at LOAD SUCCESSFUL.
tags: save,continue,autosave,menu,re,instrumentation
created: 2026-08-22
updated: 2026-08-22
---

## Root causes and implemented Continue path

Retail ships no Continue row. Its six authored main-menu rows contain New Game,
Load Game, Danger Room, Review, Options and a nonfunctional Online Multiplayer
entry; two rows also have hardcoded special handling outside their commands.
`src/native/continue_runtime.c` retains `CMenuMain::Show` at 0x005c9260 and
then applies one reversible six-row plan. A valid catalog gives Continue, New
Game, Load Game, Danger Room, Review, Options; without one it restores New Game
through Options and hides the last row. It shifts original command handles,
moves the Danger comparator with its row and disables the retired Online
special case. Load Game remains available for older slots.

The first integration build scanned the writable profile root directly, but
retail saves are one level of ownership deeper at
`Activision/X-Men Legends 2/Save`. That made existing saves invisible to both
boot and menu Continue. `src/save/save_directory.{c,h}` is now the single path
authority: the platform still owns the writable root, while boot, Continue and
autosave consume the exact retail leaf directory. A missing leaf directory is
the normal first-run empty-catalog result, not an I/O failure.

Continue reuses registered callback 0x005f2b70. It asks the retail manager for
mode 3, reads the exact leaf's 0x80-byte header and selects its metadata slot.
Only manager mode 3/state 0x1c accepts the transaction. A one-shot 0x0055ff00
override then substitutes the exact catalog leaf into the retail arbitrary-leaf
reader at 0x0055fcd0. The greatest `(mtime_ns, leaf)` candidate is authoritative
and there is no corrupt-newest fallback. Boot Continue consumes its cached
request only after manager acceptance.

The first live Boot Continue then proved the exact `saveslot0.save` payload read
but stopped at the retail LOAD SUCCESSFUL modal. That is correct for manual
Load and incomplete for native Continue: 0x004b1280 constructs the mode-3
success dialog and only then sets manager state 1; Enter dispatches plain-RET
callback 0x0049f140, whose manager vslot +0x58 completes deserialization.
Native Continue now arms a separate one-shot only after mode3/state1c
acceptance. The retained 0x004b1280 body runs first; only mode 3/state 1 consumes
the one-shot and invokes the same retail callback. A failed payload read,
unexpected completion state or return to the main menu clears it, so manual
Load never inherits automatic acknowledgement and still presents the modal.
The completed Boot Continue then reached Sanctuary through this exact path.

## Live evidence already obtained

A 2026-08-22 native run observed `main.engb` 3/3, one Build and one Show. A
manual `saveslot0.save` load reached 0x0055fcd0, manager 0x004aed10 with
mode=3/state=1/device=0/selection=0, deserializer 0x0046e2b0 with the same
buffer, then mode 3 -> 0 and Sanctuary with LOAD SUCCESSFUL. An extraction save
queued `saveloadProcess(4)`, reached state 0x1d and both exact writer return
sites, then replaced the 195,716-byte host file and advanced its mtime. Static
correction: 0x0049f140 is CompleteLoad, 0x0049f150 is DeleteCorrupt,
0x0049f860 is `lockCombat`, and 0x004a6b50/0x004a6d01 identify extraction's
save command rather than a stable checkpoint.

## Autosave root cause and implemented boundary

`src/save/autosave_storage.c` publishes a retail-compatible `autosave.save`:
128-byte header, little-endian payload length and serializer payload. It uses an
exclusive same-directory temporary, complete-write checks, file fsync, atomic
rename and directory fsync. Injected failures at every pre-rename phase prove
the prior autosave survives and no temporary remains.

The proposed map byte `+0x221 & 8` was not a no-save gate: it remained set in
Sanctuary after a successful Continue even though a manual Sanctuary save had
already succeeded. That live counterexample falsified the predicate. The
implemented policy therefore depends only on observed retail transitions. A
successful retained 0x00484ce0 map return queues MAP_LOAD; retained
`CMenuMain::Show` cancels the pending request so the initial main-menu map can
never write. A busy save manager resets the debounce, and the request fires
after 64 consecutive guest input polls with manager mode zero. Each checkpoint
is attempted once; a later successful map return is required after failure.

`src/native/autosave_runtime.c` calls the exact retail serializer owner from
0x0046dce0 and its vslot `+0x208` into the observed 0x2fc00-byte payload. The
payload must begin with the observed `\n[SAVEGAMEBEGIN: <description>]` tag;
the shared parser extracts that description into the NUL-padded 128-byte retail
header before the transactional publisher writes `autosave.save`. The map
wrapper and menu cancellation are production behavior registered regardless of
`X2_SAVE_TRACE`; tracing only controls the bounded evidence markers.

The live `/save` report exposes map successes over map returns, scheduled and
menu-cancelled checkpoints, idle polls, manager mode, deferred polls and actual
attempt/success/failure denominators plus the last failure class and `errno`.
I067 is trusted by the completed compatibility observation: a first run began
without an autosave and reported `map-success=2/2 scheduled=2 cancelled-menu=1
idle-polls=64 attempts=1/2 success=1/1 fail=0/1 last=succeeded`, then produced
a 195,716-byte `autosave.save` without changing `saveslot0.save` size or mtime.
A second Boot Continue named that exact leaf at 0x0055fcd0, crossed the retail
0x0049f140/0x004aed10/0x0046e2b0 mode-3 chain, transitioned to mode zero and
rendered Sanctuary. C246 records the result and falsifier.

## Falsifier

Continue is falsified by a wrong row/order, rejected manager state, wrong leaf,
any path other than the retail mode-3 chain, a native Continue that still waits
at LOAD SUCCESSFUL, or a manual Load whose modal is skipped. Autosave is
falsified if the main-menu map writes, mode remains nonzero during the 64-poll
window, the report claims success without a complete atomic file, the prior
file is lost under an injected pre-publish failure, or retail cannot load the
published `autosave.save`. No alternate leaf or corrupt-newest fallback is
allowed if a future validation fails.
