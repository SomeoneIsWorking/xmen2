---
id: C250
kind: claim
status: holds
created: 2026-08-22
tags: graphics,shadows,native
depends: src/gpu/gpu_shadow.c, src/gpu/shadow_policy.c, src/gpu/gpu_shadow_selftest.c, src/gpu/shaders/shadow_depth.frag, src/gpu/shaders/d3d8_fixed.frag
---

## Claim

The native shadow-map enhancement samples true light-cast occlusion for fixed and VS 1.1 skinned title geometry

## Evidence

Production gpu_shadow_selftest: enabled pass darkened 182 pixels, disabling it and removing the caster each removed the same 182, with 3914 bit-identical controls. Bounded tutorial live run: 295017 GPU draws; 179631 casters/123420 receivers including all 11836 programmable VS draws; 0 renderer refusals and 0 shadow resource/pass failures; captured 800x600 frame had 81795 colours.

## What would falsify it

If either production A/B answer becomes identical, caster removal no longer removes occlusion, a live programmable draw is excluded, or the shadow pass reports a resource/pass failure
