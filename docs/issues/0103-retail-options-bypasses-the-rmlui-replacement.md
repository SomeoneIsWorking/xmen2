---
id: 103
title: Port settings must not replace the retail Options routes
status: resolved
symptom: Port settings were hidden behind F1 unless both authored retail Options commands were replaced
tags: pc,native,menu,rmlui,options,architecture
created: 2026-08-22
updated: 2026-08-24
---

Root cause: the first integration treated two authored routes as spare host hooks.
The main menu's `options_main` callback and the pause menu's `options` callback
were both overridden to open RmlUi, so players lost both retail Options pages.
F1 remained a second, host-only route and made the replacement look complete
without giving it a coherent place in the game's menu ownership.

Resolution: both retail callbacks are untouched. The derived native asset pack
extends the user-supplied pause XMLB with the presentation IGB's reserved
`button10` model/highlight and a distinct **PORT SETTINGS** row. That row emits
the new `port_settings` command. A subsystem-owned override super-calls the
retail command registrar at XMen2.exe `0x005f4900`, then registers only that new
command through the retail registry's own vtable method. F1 was removed. The
additive registration is idempotent if the retail registrar is invoked again.
The focused tests pin the super-call, exact command/guest ABI, absence of
overrides at `0x005f1c50` and `0x005f1fa0`, preservation of the authored
Options row, and refusal when the source menu or reserved button capacity is
not exact.
