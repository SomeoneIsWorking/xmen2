# title: Intermittent /GS stack-cookie crash in the menu-driven level load, exposed by the 9x faster load

**symptom**: the menu-driven smoke run (New Game -> difficulty -> level load) intermittently aborts with "crt: the guest's stack-check handler fired -- a buffer overrun was detected inside recompiled code", right after the difficulty confirm (frame ~4135) and after the WSAStartup no-network check, about 1 in 2-3 smoke_loop runs. The boot-direct path (X2_BOOT_MAP) never crashes.

**tags**: native,crash,load,party,memory,intermittent,timing

## Evidence

The crash is MSVCR71's `__security_error_handler`, called by `__security_check_cookie` when a /GS-protected guest function's stack cookie is corrupted. The last dispatched body is `FUN_005c6850` (XMen2.exe, a virtual method that stores a string into the global slot table at 0x008adab8 indexed by `[this+0xa0]`); the ring snapshot at the crash shows the tutorial character/animation setup path (igQuaternionf::slerp, igTransformSequence getMatrix, etc.) running.

It is TIMING-EXPOSED, not a new fault in the ported code: with the dispatch-lookup speedup (C210) the level-load frame fell from 4592 ms to ~500 ms, and the crash appeared at ~1-in-2 to 1-in-3 smoke runs. A/B test: reverting find() to the linear scan (same dispatch RESOLUTION, slow timing) passed 4/4 smoke runs with zero crashes. So the corruption is pre-existing and was masked by the slow load's timing -- the faster frames change when the game's time-based load logic (party build, conversation/timers, thread rendezvous) runs relative to the frame-driven input, reaching a code path that corrupts a stack.

## What it is NOT

Not a dispatch-table bug: binary search agrees with linear on every EP and gap in every module (C210). Not the register_override refactor (smoke passed with it before the speedup). Not the profiler (two stores per dispatch).

## Next step to root-cause

Name the /GS function whose cookie fails (a DIRECT-call callee beneath the FUN_005c6850 dispatch frame) -- a trace build (X2_NATIVE_TRACE) or narrowing the frame that overflows. Candidates: a recompiled function with a large stack buffer in the party/roster setup path, or a game bug the original also has under this timing (compare against the stock oracle's behavior if it can be driven to the same point).

## Update (2026-08-17): the writer is NOT the CRT mem stubs

Converted `__security_check_cookie` (0x00672161) to a native override that, on mismatch, names the caller. It reports caller 0x0046badd = inside `FUN_0046b750` (the game-mode/startup config setup, /GS-protected), stored cookie 0. Guest-stack dump at the crash shows a zeroed 16-byte block [0x700ffed0, 0x700ffee0) covering the cookie slot at 0x700ffed8, and a "packages/generated/maps/package/permanent_fightstyles.pkgb" path string built on the stack 200 bytes below -- so the corruption is in the fast-startup/package path.

A memory-write watch on the cookie slot fired only at the dispatch boundary (naming FUN_00679fa0, a static-array ctor that writes a STATIC at 0x70b888 -- a red herring), and the CRT mem-copy stubs (memcpy/strncpy/memset/strncat/memmove) never fired -- so the zeroing is an INLINE write in a recompiled body, not an IAT stub copy. The exact writer is still unpinned; it is a guest-stack overrun by a body in FUN_0046b750's direct call tree that reaches the cookie slot only when the load is fast enough to change the startup timing.

## Update (2026-08-18): it is NOT a buffer overrun -- the epilogue reads the wrong slot

The framing above ("a function wrote past its own stack frame") is WRONG, and
the /GS message that suggested it is the guest's own wording, not a diagnosis.
Measured on a deterministic repro:

- `FUN_0046b750` is entered with esp `0x700ffee4`, and stores its cookie at
  entry_esp-4 = **`0x700ffee0`** (`MOV [ESP+0x20],EAX` after `SUB ESP,0x1c` +
  `PUSH ESI` + `PUSH EDI`).
- Its epilogue reads `[ESP+0x20]` = **`0x700ffedc`** -- four bytes LOWER.

So the cookie was never clobbered. The body returns to its epilogue with esp
one dword below where it started, the epilogue compares a word that was never
the cookie (it happens to hold 0), and `__security_check_cookie` correctly
reports a mismatch. The corruption everyone was hunting does not exist; the
defect is a 4-byte guest stack imbalance inside `FUN_0046b750`'s call tree.

Evidence that the old hunt could not have worked: a write watch armed on the
cookie slot saw **2,000+ writes and zero stores of the process cookie**, because
it was watching `0x700ffedc` -- the slot the epilogue reads, not the slot the
prologue writes. The two addresses being different IS the bug.

### Where the imbalance is NOT

`tools/stackcheck.py` (new) checks every dispatched call's esp delta against
the callee's own `RET` immediate. Over the whole crashing run: **1,323,140 of
1,400,175 dispatched calls checked (94%), zero out of balance**, with the 19
native overrides included in that count. So the drift is not at a dispatched
call boundary and not in a hand-written override.

Four apparent offenders in the first pass (`FUN_0053f850` x83, libIGGfx
`0x10025ac0` x24, libIGSg `0x10047360` x3, `FUN_005831f0` x2) were the
checker's own defect, not the game's: those bodies end in a TAIL CALL as well
as a `RET`, so their stack effect belongs to the tail callee and cannot be read
off their own `RET`. 9,410 such bodies are now excluded by name and counted.

### Next step

The imbalance is inside a body or at a DIRECT C call, which is the one thing
the dispatch-boundary check cannot see. Naming it means checking direct calls
too -- an emitter change that brackets each emitted direct call with the same
esp expectation, enabled for one module or one call tree at a time.

### Also corrected: the write watch was lying by design

`X2_WRITE_WATCH` was ONE-SHOT. On a guest stack address -- reused by every
frame that passes through it -- the single shot was spent on an unrelated
frame's write hundreds of frames before the event, and the watch then sat
disarmed through the window it was armed for and reported nothing. It now
reports every write, takes an optional `:<value>` filter, and always reports
`/GS` cookie stores and stores of zero. See `docs/info/instruments/`.
