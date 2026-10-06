---
id: C254
kind: claim
status: holds
created: 2026-08-24
tags: ui,menu,rmlui
depends: src/native/options_menu.c#x2_override_005f4900, tools/make_port_pause_menu.py#derive_pause_menu, tests/test_options_menu.c#main
---

## Claim

Port Settings is an additive pause-menu route and the two retail Options callbacks remain unmodified

## Evidence

The focused options_menu production-seam test super-calls 0x005f4900, registers exactly port_settings through the retail vtable, rejects overrides at 0x005f1c50/0x005f1fa0, and proves repeat invocation is idempotent. The pause generator produced byte-identical outputs for all four real user assets, each with one retail Options and one Port Settings row.

## What would falsify it

A shipping pause variant lacks exactly one Port Settings row, either retail Options callback is overridden, or the focused production-seam test fails.
