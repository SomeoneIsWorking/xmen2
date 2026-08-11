---
id: 53
title: Depth-tested geometry drew in submission order: there was no depth target
status: resolved
symptom: world geometry paints over things in front of it; 476531 draws asked for a depth test there is no target for
tags: d3d8,gpu,depth
created: 2026-08-11
updated: 2026-08-11
---

## Symptom

A menu run reported `476531 draw(s) asked for a depth test there is no target
for; they drew in submission order`, and the picture showed background
geometry painted over foreground.

## Cause

The device's automatic depth surface (the game asks for `depth=auto`, D16) was
a `IDirect3DSurface8` with no GPU texture behind it, so `gpu_pass_begin`
attached no depth-stencil target and `pipeline_for` set
`has_depth_stencil_target = false`. The depth state was carried through the
whole pipeline key and then dropped -- honestly reported, and still wrong.

## Fix

`gpu_depth_target()` creates one sized to the current target, and
`gpu_depth_format()` picks the format by ASKING the driver
(D24_UNORM_S8_UINT, then D32_FLOAT_S8_UINT, D24_UNORM, D32_FLOAT, D16_UNORM),
because which depth format exists is a driver property.

The format is resolved on first ASK, not when the first pass opens: a pipeline
is built BEFORE the pass it draws into, and it must declare the same format the
pass attaches. Deciding it at pass time would have made the first pipeline of
every run declare "no depth" against a pass that has one.

The depth attachment always CLEARs on pass entry. Loading the previous frame's
Z would depth-test this frame against the last one's geometry.

## Covered by

`--d3d8-selftest`'s depth self-test: a NEAR red quad drawn first, a FAR blue
one second, and the centre pixel must come back red. Run against a deliberately
detached depth target, it reports the blue.

## It also caught a second defect

The 1x1 placeholder texture's handle was kept across `gpu_draw_shutdown()`,
which empties the resource table -- so the next device's first untextured draw
looked up a handle that no longer existed and was refused. The self-test is the
first thing in this codebase to create a second GPU device in one process; the
game does the same on any `Reset`.
