---
id: C240
kind: claim
status: holds
created: 2026-08-22
tags: pc,graphics,shadow,oracle
depends: tools/proxy_d3d8/shadow_setting.c#shadow_setting_install_query_override, tools/proxy_d3d8/proxy.c#shadow_proxy_init
---

## Claim

The stock DetailedShadow control now substitutes the retail registry read rather than a backing byte that startup overwrites

## Evidence

A real X2_SHADOW_FORCE=0 run first reproduced the old defect: control observed 0 at Direct3DCreate8, then F9 emitted detailed_shadow_mismatch actual=1. Static recompiled XMen2 shows FUN_00619770 calls the registry DWORD reader and stores AL at 0x006198c3 after Direct3DCreate8. With the IAT seam, matched live runs recorded original registry=1, forced/read count=1, and retained live bytes 0 and 1 respectively; the comparator got past control validation and refused only because the selected scenes reached path=none.

## What would falsify it

A supported XMen2 executable whose RegQueryValueExA import cannot be validated, a run with forced_reads=0, or a selected frame whose live backing byte differs from the substituted value
