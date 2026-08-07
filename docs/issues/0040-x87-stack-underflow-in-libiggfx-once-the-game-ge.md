---
id: 40
title: x87 stack underflow in libIGGfx once the game gets past the save check
status: resolved
symptom: x87_fault: x87 stack underflow, ~30s into a --d3d8 --run, with the ring's last entries FUN_100c179b / FUN_100c1778 / FUN_100c167e in libIGGfx called from 0x100c24a3, 0x100c24d0, 0x100c24da
tags: pc,native,recomp,x87,libIGGfx
created: 2026-08-07
updated: 2026-08-07
---

## Where it happens

Only reachable since issue #39 was fixed: with the save check passing, the game
runs on into code it had never entered. The title screen renders fully first
(scratch/screenshots/after.png), then:

    x87_fault: x87 stack underflow
      This is the MODELLED x87 stack, so it is a translation defect, not a
      guest bug: some body pushed or popped a different number of times than
      the original.

The ring names the neighbourhood:

    enter libIGGfx!0x100c179b FUN_100c179b  <- 0x100c24a3
    enter libIGGfx!0x100c1778 FUN_100c1778  <- 0x100c182b
    exit  ...
    enter libIGGfx!0x100c167e FUN_100c167e  <- 0x100c24d0
    exit  ...
    enter libIGGfx!0x100c167e FUN_100c167e  <- 0x100c24da

## Root cause: the REGISTER forms of FST/FSTP were both wrong

Not a function boundary. The three bodies the ring named have proper MSVC
prologues (`PUSH EBP; MOV EBP,ESP`) and are real functions -- read, not
assumed, which is what took that hypothesis off the table.

The fault is in `FUN_100c2105`, and the translator emitted this for the
register forms:

```python
if m in ("FSTP", "FST", "FISTP", "FIST"):
    if st:
        return [A, "X87_ST(C, %s) = x87_pop(C);" % st.group(1)]
```

Two defects in one line:

* **`FST ST(i)` does not pop.** Emitting a pop drains the modelled stack one
  slot per execution until it underflows. 2 sites in the image.
* **`FSTP ST(i)` pops, but it stores FIRST.** Writing the popped value into
  `X87_ST(i)` indexes the POST-pop stack, so `FSTP ST(1)` landed in what had
  been ST(2), and `FSTP ST(0)` -- a discarding pop -- overwrote the new top
  with the value it had just discarded. **2981 sites in the image**, silently
  wrong in the values rather than in the depth.

Four cases in `tests/test_recomp.py::X87RegisterStores`, including that
`FSTP ST(0)` must not write over the new top.

After re-emitting every module the underflow is gone and the run reaches an
ordinary discovery-loop input: an indirect dispatch to `XMen2.exe 0x0049f7e0`
with no recompiled body.

## What made it findable

`x87_fault` named the invariant and not the place. It now prints the boundary
ring AND the host return address of its caller, as a runnable `addr2line`:

    x87_fault: x87 stack underflow
      the body that did it:  addr2line -fCe <this binary> 0xd5d044

`x87_pop` is inlined into the generated body, so `addr2line -i` walks straight
through it to `fn_libIGGfx_100c2105` and the emitted line -- whose comment
carries the guest address, `100c26b4 FSTP ST0`. The ring gave the
neighbourhood; this gave the instruction.

## The hypothesis that was wrong, and why it was worth writing down

Those entry points are NOT aligned (0x100c167e, 0x100c1778, 0x100c179b) and
they are tiny. That is the signature of a function boundary that is wrong --
either a mid-function address seeded as a function start (seed_code_imms takes
code immediates, and a callback address that is really mid-body looks
identical), or a body truncated so its x87 pushes live in a different
"function" from its pops. This project has hit that class before: C077 and
issue #8, where a detected function sat INSIDE another's body.

So the first question is not "which FSTP" but "are these real functions". Check
them against the shipped PE with tools/verify_export.py and read what precedes
0x100c167e: if the preceding bytes fall through into it, it is not a function.

## What the instrument now says

`x87_fault` used to print four words and abort, which named the invariant and
not the place. It now dumps the boundary ring first -- the depth is a property
of a translated body, so the last bodies entered and who called them is exactly
the evidence needed, and it was being thrown away.
