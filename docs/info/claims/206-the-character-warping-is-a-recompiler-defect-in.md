---
id: C206
kind: claim
status: holds
created: 2026-08-15
tags: recomp,animation,skinning,geometry,libigmath
---

## Claim

The character warping is a RECOMPILER defect in the engine's animation maths, not a renderer defect: the port's bone matrix palette is not made of rigid transforms, while the control's is entirely rigid.

## Evidence

tools/constcmp.py over both sides' vertex-shader constant files, captured on an F9 frame of the same dialog. 83 bone slots in common from c[6] (3 registers each, read off the shader's own ), 32 written by either side. STOCK: 32 of 32 written bones are rigid transforms. PORT: 6 of 32. All 32 differ. Bone 11's port rows have lengths 0.97, 1.32 and 0.75 where every control row is exactly 1.0 -- a matrix whose rows are not unit length is not a rotation, so this is not an animation-timing difference between the two runs. Example, bone 20: port row1 (-0.7600, -0.4148, 1.0462) against control (0.9998, -0.0197, -0.0098). The palette is computed by the engine's own code, which is recompiled x86 in the port and native x86 in the control, and it crosses the D3D8 boundary as SetVertexShaderConstant -- so the divergence is upstream of every part of this renderer. constcmp.py's selftest separates identical palettes, a single corrupted bone, a rotation from a scaled rotation, and refuses empty or missing input.

## What would falsify it

a control capture in which the stock palette is also non-rigid -- which would make row length a property of this engine's matrices rather than a defect -- or a port run whose palette becomes rigid without any change to the recompiled maths
