---
id: C255
kind: claim
status: falsified
created: 2026-08-24
tags: registry,resolution,settings
depends: src/native/registry_view.c#x2_registry_view_init, src/native/advapi32.c#imp_ADVAPI32_RegQueryValueExA, tests/test_registry_resolution.c#main
falsified_on: 2026-08-24
---

## Claim

The retail Resolution registry value is one coherent host-owned view of the current x2native width and height

## Evidence

test_registry_resolution drives the shipping ADVAPI boundary through key open, child/value enumeration, query, short-buffer refusal, stale 800x600 guest write, host change to 2560x1440, and an ordinary FSAA value; query and enumeration agree and no duplicate Resolution or Display appears.

## What would falsify it

Direct query and enumeration disagree, a stale stored Resolution replaces the host value, or the boundary test fails.

## FALSIFIED 2026-08-24

A bounded 3840x2160 host run refuted the claimed shared authority: deriving the retail Display/Resolution registry value from host settings changed the guest logical D3D backbuffer from the retail 800x600 mode to 3840x2160 and visibly shrank game text. Host window/swapchain output resolution and the guest logical backbuffer are independent policies.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
