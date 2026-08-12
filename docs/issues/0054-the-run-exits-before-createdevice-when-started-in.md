
## A DETERMINISTIC repro, and why it is not a fix (2026-08-12)

This issue never had a reliable repro. It has one now:

    # 0 of 16 reached CreateDevice
    X2_QUANTUM=0 tools/smoke_loop.sh          # backgrounded (nohup)
    # 21 of 21 reached CreateDevice and closed the whole game loop
    tools/smoke_loop.sh                       # same script, same shape

Both backgrounded, both setting `X2_SHOT`, both with `X2_MAX_FRAMES`, same
binary, same machine, same session. The ONLY difference is `X2_QUANTUM`.
Fisher's exact on 16 failures against 5 passes is p ≈ 5e-5, and the 16-run
sample above it agrees. In the FOREGROUND, `X2_QUANTUM=0` reaches CreateDevice
fine -- so the trigger is backgrounding, and the quantum is what makes it
survive.

**And the quantum cannot be doing what it was written to do here.** The failing
runs report "threads: no guest thread was ever created" -- the exit happens
before the game starts any thread, so there is nobody for a preemption to yield
to. `guest_quantum()` at that point reads `g_waiters`, finds 0 and returns.
What `X2_QUANTUM=20000` actually changes is that a function call and a volatile
read happen every 20,000 dispatches. That is a TIMING PERTURBATION, not
scheduling.

So: the cause of this issue is still not understood, and preemption is MASKING
it. Recording it as fixed would be recording a bandaid. What is genuinely new
is the repro -- an intermittent timing bug now has a switch that turns it on
0 times out of 16 -- and that is what makes the cause findable. The next step
is to find WHERE the backgrounded run exits: it gets as far as the
`X2_UNPACED` line and then leaves without an abort, an unsupported instruction
or a dispatch stop, which means the guest returned from its entry point.

This also corrects what I wrote against issue #57 earlier today: "the quantum
fires and changes nothing observable" is WRONG. It changes whether a
backgrounded run starts at all. It remains unattributed for #57's movie
deadlock, which is a different failure.


## ROOT CAUSE, found by reverse-engineering the exit path (2026-08-12)

Not a race, not backgrounding, not scheduling. **The native override of the
DirectX 9.0c presence check returned no value, and the game tests one.**

The path, read out of the exe rather than inferred from timing:

    __tmainCRTStartup 0x006725f4
      0x00672774  CALL 0x00403420        ; WinMain
      0x0067278a  CALL [0x0067f1a8]      ; exit(WinMain's return)

    WinMain 0x00403420
      0x004034d7  CALL 0x00617480        ; the DirectX check -- OVERRIDDEN
      0x004034dc  TEST AL,AL             ; <-- it tests the RETURN VALUE
      0x004034de  JNZ 0x004034ee
      0x004034e0  MOV byte [0x006f3c2c],1    ; "the DirectX check is why"
      0x004034e7  MOV byte [0x006f3a2d],1    ; "graphics init must fail"
      ...
      0x00403533  MOV AL,[0x00a09f94]    ; the quit flag
      0x0040353b  JNZ 0x00403567         ; set -> SKIP THE ENTIRE MAIN LOOP

    display init 0x005fb270
      0x005fb27e  MOV AL,[0x006f3a2d]
      0x005fb292  JZ  ...                ; set -> 
      0x005fb294  MOV byte [0x00a09f94],1    ; the quit flag

The original 0x00617480 ends `XOR AL,AL` / `MOV AL,1` -- a BOOL in AL. The
override's comment said "void __cdecl FUN_00617480(void)" and it left EAX
alone, so the game tested whatever the previous call had left there. When that
byte happened to be zero, WinMain set both flags, the display initialiser set
the quit flag, WinMain skipped its whole main loop and returned 0 -- and
skipped the `DISPLAY_FAILED` / `UNABLE_TO_INITG` message box as well, because
0x006f3c2c said the DirectX check was the reason. A silent `exit(0)` before
CreateDevice with no thread ever started: this issue, exactly.

That accounts for every property it had. Intermittent, because leftover EAX
depends on what ran before. Sensitive to backgrounding, to X2_SHOT, and to the
preemption quantum, because all of them change what ran before. And silent,
because the same wrong branch suppresses the error box that would have named
it.

**Fix**: the override writes the answer -- AL = 1, low byte only, exactly as
the original's true path does. `src/native/overrides.c`.

**Verified**: `X2_QUANTUM=0`, the configuration that failed 18 of 19 times
before, now passes 4 of 4 (the earlier 300s failures were the timeout being
too short for the slower no-preemption run, which reaches frame 4044 at
t=304s, not the early exit -- CreateDevice happens in all of them).

**The general defect this is an instance of**: an override must reproduce the
original's RETURN VALUE, not just its stack effect. Both were documented here
and only the stack effect was right. Check the CALL SITE, not the decompiler's
signature -- Ghidra typed this one `void` too.
