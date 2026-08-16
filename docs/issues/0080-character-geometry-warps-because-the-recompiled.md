---
id: 80
title: Character geometry warps because the recompiled animation maths produces non-rigid bone matrices
status: open
symptom: Characters are misshapen in gameplay and dialogs -- Cyclops's head reads as collapsed to a plane. Level geometry, UI and lighting are correct. Unchanged by every renderer-side fix, and identical through the fixed-function path and the vertex-shader path.
tags: recomp,animation,skinning,geometry,libigmath,d3d8
created: 2026-08-15
updated: 2026-08-15
---

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

- The renderer. C202 (buffer upload ordering), C203/C206 (the vertex buffers
  themselves match the control on 73 of 75 meshes), and the whole D3D8 light
  path are all clean.
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
