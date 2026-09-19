# 0164 — the browser build kept every call between translation units

- **State items:** S021
- **Status:** fixed; `tools/build_web.py` configures with
  `CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`
- **Found by:** reading the #162 profile after the x87 memory work, where five
  separate softfloat frames sat under one arithmetic helper

## The cause

The guest's x87 arithmetic is a chain of one-line functions, each in its own
translation unit:

```
x86p_x87_arith_raw            x87.c
  x86p_x87_software_arith_raw x87_softfloat.cpp
    extF80_add                softfloat3e/extF80_add.cc
      softfloat_addMagsExtF80 softfloat3e/s_addMagsExtF80.cc
        softfloat_roundPackToExtF80
                              softfloat3e/s_roundPackToExtF80.cc
```

Every one of those calls survives `-O3`, because `-O3` cannot see across a
translation unit. The chain runs once per guest FADD, and the profile showed
exactly that shape: `x86p_x87_arith_raw` at 10.56% with `extF80_add` 1.77%,
`softfloat_subMagsExtF80` 1.25%, `softfloat_roundPackToExtF80` 1.35%,
`softfloat_addMagsExtF80` 1.05% and `softfloat_normRoundPackToExtF80` 0.88%
beneath it as separate frames.

The web build never asked for link-time optimisation. Nothing chose against it;
the configure line was written before there was a profile to read.

## What changed

One configure argument in `tools/build_web.py`. The bitcode it produces is the
same program, so nothing in the source moved and nothing about which compilers
the project supports changed -- this is the product's own release build asking
its own toolchain to inline across the files it already links together.

## The mechanism, checked in the profile

Two 20-second windows of the Dead Zone route, same build inputs apart from the
flag, normalised shares of the guest worker:

| frame | -O3 only | with LTO |
|---|---|---|
| `x86p_x87_arith_raw` | 10.56% | 13.18% |
| `extF80_add` | 1.77% | **absent** |
| `softfloat_normRoundPackToExtF80` | 0.88% | **absent** |
| `x86p_x87_software_narrow_raw` | 2.76% | **absent** |
| `x86p_x87_reg_from_operand_bits` | 8.43% | 4.84% |
| `x86p_x87_operand_bytes_from_reg` | 2.02% | 4.16% |
| `x86p_jit_engine_run` | 8.52% | 10.54% |

Read the risers as the mechanism, not as regressions: `arith_raw` went up
because `extF80_add` and two of its callees are now inside it, and
`operand_bytes_from_reg` went up because `software_narrow_raw` is. The frame
that actually got cheaper in absolute terms is `reg_from_operand_bits`, the
f32/f64 widening, which nearly halved once `f64_to_extF80` could be inlined and
its status argument constant-folded.

## The frame rate, and why it is stated as a plateau

The host was running another agent's build the whole time -- nine concurrent
`clang-tidy` processes, an Android emulator, load average about 10 on 16 cores
-- and it shows in the samples as a mid-run collapse to roughly 6 presents/s
that appears at a different elapsed time in every run, including a run where it
never appeared at all. **Do not read a median across a contended run as this
route's frame rate**; the first table of numbers in this file was thrown away
for exactly that reason.

What is comparable is each run's uncontended plateau, and those do not overlap:

| build | plateau, presents/s | runs |
|---|---|---|
| -O3 only | 9.60 – 9.80 | 3 |
| with LTO | 11.40 – 11.60 | 3 |

About 18%, and the two back-to-back runs that produced those exact bands were
taken minutes apart with nothing else changed.

`tools/web_presents.py` is what produces the tables. It reads the console
through `tools/web_console.py` -- which attaches to the workers, where the
heartbeat is actually written -- rather than through WebLua's fifty-line ring,
and it reports the plateau beside the median so a contended run cannot pass as
a slow build. Both of its refusals were exercised: a log with no heartbeat, and
a log with heartbeats and no frames.

## What would falsify this

A run of both builds on a quiet machine whose plateaus overlap. The profile
shape would still be right -- the frames really did merge -- but the 18% would
be contention noise that happened to land the same way twice, and the honest
number would be whatever the quiet machine says.

The narrower falsifier is build size: LTO took the module from 9,655,882 to
9,807,856 bytes, which is inlining, not code the linker failed to drop. If a
later build shows that growing out of proportion, the trade has changed.

## What this does not cover

The native desktop build has the same four-deep chain and the same flag is
absent from its configure. Nothing here measured it, so nothing here claims it;
the browser was the target because the browser is where the frame rate is.
