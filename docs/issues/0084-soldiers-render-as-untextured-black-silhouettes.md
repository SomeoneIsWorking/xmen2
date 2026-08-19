---
id: 84
title: Soldiers render as untextured black silhouettes, and one lies in a dead pose while still fighting
status: open
symptom: an enemy soldier draws as a solid black silhouette with its weapon fully textured, and another soldier lies flat on the floor in a dead-looking pose during a live fight
tags: pc,native,graphics,d3d8,textures,animation,characters,user-report
created: 2026-08-19
updated: 2026-08-19
---

REPORTED BY THE USER, 2026-08-19, from a real play session. Not yet reproduced
by an agent and not yet root-caused: this entry is the observation, kept
separate from any theory about it.

Capture: `scratch/shots/report-2026-08-19-black-soldier.png` (gitignored -- it
is a frame of the shipped game).

## What is in the frame

* A standing soldier is a **solid black silhouette** -- no diffuse at all -- while
  the RIFLE HE HOLDS is fully textured and lit, and his muzzle flash draws
  correctly. So this is per-mesh or per-material, not a whole-draw or whole-frame
  failure, and it is not the lighting path being off for the scene.
* A second soldier lies **flat on the floor in a dead/prone pose** in the middle
  of a live firefight.
* The player character (Wolverine) beside them is textured and shaded correctly,
  and so is the level geometry, so the failure is confined to some subset of
  the character draws.

The user's words: "soldiers sometimes look like this (body on the floor like
dead, untextured black guy shooting)". SOMETIMES is the important word -- the
same enemy type draws correctly elsewhere in the same level, so whatever
selects the failing case is intermittent.

## Why the two halves may be one defect or two

They are logged together because they appear together in one frame and both are
about the same actor class, but they are not obviously the same fault:

* black-with-textured-weapon is a TEXTURE or MATERIAL selection failure on the
  character's own mesh (a skinned mesh, where the weapon is a separate attached
  static one).
* the prone pose is an ANIMATION state failure -- either a death animation
  played on a live actor, or a live actor whose animation never started and is
  sitting in its bind/first-frame pose.

A single upstream cause that fits both is the SKINNED path specifically: this
port only reaches SSE `alignedMatrixMultiplySSE` on the first level with an
animated character (see the recompiler note in docs/codemap.md), and a skinned
character that gets a wrong bone palette can both collapse to a flat pose and
lose the material state that the attached weapon, drawn unskinned, keeps. That
is a HYPOTHESIS with no evidence behind it yet -- do not treat it as the cause.

## What the next session should do first

1. Reproduce with a boot-map run and the frame instruments that already exist,
   rather than by eye: `X2_SHOT_AFTER_FILE=<level>` to gate the capture on the
   level being loaded, `X2_FRAME_DUMP=busy` for the draw list of the frame the
   soldier is in, and the texture-stage histogram in the shutdown report.
2. The discriminating question is answerable from that dump alone: does the
   black soldier's draw have a texture bound at all (handle 0 / untextured), or
   is one bound and sampling black? The histogram already separates UNTEXTURED
   draws from disabled stages, so this is a count, not an inspection.
3. Only then decide whether the pose is a second issue and split this entry.

Related: issue #62 (gameplay renders almost entirely black) is a DIFFERENT
symptom -- whole-scene, and resolved -- but its instruments (the light dump,
`tools/lightlog_diff.py`) are the ones to reach for here.
