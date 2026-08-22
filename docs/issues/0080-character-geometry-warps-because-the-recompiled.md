---
id: 80
title: Character geometry warps because the recompiled animation maths produces non-rigid bone matrices
status: resolved
symptom: Characters are misshapen in gameplay and dialogs -- Cyclops's head reads as collapsed to a plane. Level geometry, UI and lighting are correct. Unchanged by every renderer-side fix, and identical through the fixed-function path and the vertex-shader path.
tags: recomp,animation,skinning,geometry,libigmath,d3d8
created: 2026-08-15
updated: 2026-08-15
---

## Resolution: the recompiled C was STALE

Not a defect in any source file. `src/recomp/*.c` is generated and gitignored,
so nothing tracked ever showed that a module had fallen behind the translator
that produces it. `libIGSg` was last emitted before two `recomp.py` fixes
landed, and the port ran that stale build for days:

    5151a92  2026-08-13  Fix reversed x87 register arithmetic
    a7f7a44  2026-08-14  Make the hosted recomp runtime ABI-faithful

Re-emitting libIGSg with the current translator changes 1238 lines: 1232 of
them the tail-call ABI (209 sites in that module alone) and 6 the reversed
FSUBR/FDIVR at three sites. WHICH of the two fixed the geometry is NOT
isolated -- the three x87 sites are in a shadow shader, a time switch and one
unnamed function, none of them animation code, so the tail-call change is the
likelier of the two. Naming one would need a translator bisect and has not been
done.

Measured, same instrument and same measure as the original finding:

    before   port  6 of 32 written bone(s) rigid;  stock 32 of 32
    after    port 32 of 32 written bone(s) rigid;  stock 32 of 32

It came right when the module was re-emitted for an UNRELATED reason -- the
oracle probes needed it isolated into its own translation unit. That is the
worst possible way to find this, and every instrument built to chase it was
aimed at source that was already correct.

All 20 modules have been re-emitted. `tools/recomp.py` now stamps a content
hash of itself into everything it generates, `tools/check_emitted.py` refuses a
tree whose emitted C does not carry the current stamp, and CMake fails the
configure rather than building one. Its selftest is a discriminator: a current
stamp, a wrong stamp, an ABSENT stamp, and an edited translator must produce
four different answers.

## Symptom

Characters are warped in gameplay and in the first dialog; the level, the UI
and the lighting are right. The defect is IDENTICAL whether the characters are
drawn through the fixed-function path (before C205) or through the engine's
own skinning vertex shader (after it) -- two paths that share almost nothing
in this backend.

## Cause

The bone matrix palette the engine computes is not made of rigid transforms.
Measured with tools/constcmp.py against the stock engine through
tools/proxy_d3d8, on an F9 frame of the same dialog:

    port:   6 of 32 written bone(s) are rigid transforms
    stock: 32 of 32 written bone(s) are rigid transforms
    all 32 differ

Bone 11, port row lengths 0.97 / 1.32 / 0.75; every control row is 1.0. A
matrix whose rows are not unit length is not a rotation, so this is NOT the
two runs being on different animation frames -- that objection would explain
different rigid matrices, not non-rigid ones.

The palette crosses the boundary as SetVertexShaderConstant, so it is computed
entirely upstream of this renderer, by engine code that is recompiled x86 in
the port and native x86 in the control.

## What this rules out

- The renderer for this specific finding: the non-rigid matrices were already
  present in the constant palette before the renderer consumed them. C202's
  later zero-hazard conclusion was falsified by a different scene and no
  longer supports this issue; C203/C206 still show 73 of 75 mesh buffers match
  the control, and the D3D8 light path remains clean.
- The vertex-shader interpreter, at least as the primary cause: it is fed
  corrupt matrices before it does anything.

## Ruled out, measured against the real engine

The oracle probe harness (`tools/probes.json`, `tools/oracle_compare.sh`)
records a named guest function on both sides and diffs the two streams. Both
captures are DRIVEN and unattended -- neither needs anybody at the keyboard.

Matched by INPUT CONTENT rather than by call index, because these are pure
functions of their arguments and the two runs drift badly (the port reaches
gameplay in 110 s, the control takes 540). Index alignment found 2 distinct
shared inputs and called it "no differences"; content matching found 6,066
non-trivial ones.

    igQuaternionf::slerp      2129 shared input value(s), 2127 non-trivial, 0 differ
    igQuaternionf::getMatrix  3940 shared input value(s), 3939 non-trivial, 0 differ

So the quaternion interpolation and the quaternion-to-matrix conversion are
translated correctly. Not "reached and did not crash" -- the same numbers in,
the same numbers out as the real engine, on six thousand distinct inputs.

Also measured, and a result in itself: these five probes fired ZERO times on
BOTH sides across a full driven run --

    igQuaternionf::lerp, igQuaternionf::normalize, igQuaternionf::multiply,
    igMatrix44f::multiplyAligned, igMatrix44f::setQuaternion

The bone palette is therefore NOT built by concatenating through
`multiplyAligned`, which was the leading hypothesis and is now dead. Nor is any
quaternion normalised anywhere on this path.

And separately, needing no oracle at all (the x2native battery): the recompiled
`alignedMatrixMultiplySSE` is exact and the x87 `matrixMultiply` is within
4.8e-7 of the reference on two rigid inputs, whose product must be rigid.

## The call site that fills the block, named

A census of the guest return address at `SetVertexShaderConstant` (it is on the
stack the method is called with) plus a scan of the frames above it:

    6093 call(s), ONE distinct call site, writing c[0..102]
      libIGGfx  igDxVisualContext::setVertexShaderConstant_Dx
      above     igDxVisualContext::updateVertexShaderConstants
      above     igDxVisualContext::updateContextState
      above     igDxVisualContext::drawIndexed
      above     libIGAttrs igGeometryAttr1_5::apply
      above     libIGAttrs igGeometrySetAttr::apply
      above     libIGAttrs igDisplayListAttr::apply

The frames above the first are a STACK SCAN, not an unwind -- a stale return
address in a dead frame reads the same as a live one -- so they are leads.

Probed and NEVER CALLED, across a full driven run: `igActor::getBlendMatrix`,
`igActor::getBoneMatrix`, `igAnimationCombiner::getBlendMatrix`. The palette is
not read through the accessors; the code that assembles it reaches the matrix
arrays directly, or those accessors are inlined into it.

## Where it must be, then

Between `getMatrix` producing a correct bone matrix and
`SetVertexShaderConstant` receiving a non-rigid one. That is the code that
assembles the palette -- in libIGSg or libIGGfx, not libIGMath. Probe the
writers of the constant block next; the harness takes a function name.

## Leads, none confirmed

-  has 95 3DNow! instructions (PFMUL, PFADD) across 4
  functions, which means the engine carries MORE THAN ONE SIMD maths backend
  and chooses between them -- normally on CPUID. Which backend runs in the
  port versus under Wine is not established, and a different backend is a
  different set of translated instructions. Those four functions abort by name
  if executed and nothing in the run reported that, so the 3DNow! path itself
  is not running.
- SHUFPS, UNPCKLPS and UNPCKHPS were checked against the spec in
   and are correct, so a mis-decoded lane permute is out.
-  translates 140340 of 140340 instructions; 
  47628 of 47723.

## Next

Find the function that writes the palette and compare its output directly.
 reads a live control's guest state from outside, which
is the existing tool for exactly this. The palette is uploaded via
SetVertexShaderConstant, so the call site that fills it is reachable by
working back from the libIGGfx code that calls it.
