
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
