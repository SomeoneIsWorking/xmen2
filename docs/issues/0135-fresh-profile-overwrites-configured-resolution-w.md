---
id: 135
title: Fresh profile overwrites configured resolution with retail 800x600 default
status: resolved
symptom: First launch of a fresh 3840x2160 profile creates an 800x600 D3D device and writes 800x600 back to the registry
tags: resolution,registry,cold-path,native,presentation
created: 2026-08-27
updated: 2026-08-27
---

# Fresh profile overwrites configured resolution with retail 800x600 default

- status: resolved
- state_items: S008
- tags: resolution, registry, cold-path, native, presentation

## Symptom

A bounded first launch over a newly created profile announced `DISPLAY SEED: published video 3840x2160`, but retail created its D3D8 device at 800x600 and persisted `Resolution=800x600`. Font scaling still followed the requested 3840x2160 output, producing grossly oversized text in the 800x600 logical frame.

## Root cause

The host publishes before guest code, but a fresh registry has no `Settings\Display\Version=7`. Retail `FUN_00619770` therefore takes its first-run branch: `FUN_006196c0` installs defaults, `FUN_00616df0` writes Version 7, and `FUN_00619440` persists the default 800x600 resolution over the earlier host publication. The warm-profile branch instead reads the existing Resolution, which is why previous repeated-profile evidence passed.

## Proper fix

`display_mode_runtime` wraps the verified retail settings-load boundary at
XMen2.exe `0x00619770` and calls the retained retail body through the JIT first. This keeps
every first-run default and the Version 7 transition retail-owned. The bridge
then republishes the configured mode, constructs the exact 0x78-byte retail
registry-path context through `0x00616a70`, and invokes the retail
`Resolution` reader at `0x00616e10` before device creation. It verifies the
parsed guest buffer byte-for-byte and refuses to continue if publication,
mapping, string capacity, or parsing disagrees.

The repeatable `selector-dialog-4k` live case now boots both branches over the
same isolated profile. Its cold branch and subsequent Version-7 warm branch
passed 13/13 checks: both logged `CreateDevice ... 3840x2160`, the cold branch
logged the retail-reader reconciliation and persisted ASCII `3840x2160` in
`registry.txt`, and the warm screenshot was a real 3840x2160 PNG. The cold
failure that exposed the missing `ECX` registry context was retained as the
reason the bridge builds that context through retail rather than calling the
reader as a free function.
