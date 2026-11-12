---
id: C254
kind: claim
status: falsified
created: 2026-08-24
tags: ui,menu,rmlui
falsified_on: 2026-11-12
depends: src/native/options_menu.cpp#override_005f4900, tools/make_port_pause_menu.py#derive_pause_menu, tests/test_options_menu.cpp#main
---

## Claim

Port Settings is an additive pause-menu route and the two retail Options callbacks remain unmodified

## Evidence

The focused options_menu production-seam test super-calls 0x005f4900, registers exactly port_settings through the retail vtable, rejects overrides at 0x005f1c50/0x005f1fa0, and proves repeat invocation is idempotent. The pause generator produced byte-identical outputs for all four real user assets, each with one retail Options and one Port Settings row.

## What would falsify it

A shipping pause variant lacks exactly one Port Settings row, either retail Options callback is overridden, or the focused production-seam test fails.

## FALSIFIED 2026-11-12

The pause menu has no Port Settings row: the route, its generator and the RmlUi overlay it opened are deleted. The port's settings are rows of the retail Options and its Controls and Key Bindings pages (plan 203).

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
