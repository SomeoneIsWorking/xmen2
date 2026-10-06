---
id: 105
title: DetailedShadow off trace reverts to on before the selected frame
status: resolved
symptom: X2_SHADOW_FORCE=0 records observed=0 at Direct3DCreate8 but later F9 refuses actual=1
tags: pc,stock,graphics,shadows,instrument,registry
created: 2026-08-22
updated: 2026-08-22
---

Root cause: Direct3DCreate8 is earlier than the retail display-settings load, not later. The old proxy wrote XMen2.exe+0x668d40 and read it back immediately, but FUN_00619770 subsequently queried `Settings\Display\DetailedShadow` and its store at 0x006198c3 replaced the intervention. A real stock run exposed this as two `detailed_shadow_mismatch` refusals.

Resolution: the proxy now validates XMen2.exe's ADVAPI32 `RegQueryValueExA` IAT entry and redirects only successful DWORD reads named `DetailedShadow`. It records the original registry result before substitution, counts successful forced reads, and the comparator refuses an unrecognized seam or a zero-read control. It never changes the configured Wine prefix or registry. A live OFF run recorded registry=1, substituted=0 and live backing byte=0.

The matched 3D-menu pair and matched Sanctuary save pair were still correctly refused as shadow-path evidence: all three probed Alchemy paths were zero for both settings. The gameplay OFF frame had 253 draws and ON had 256, with zero drops/hook failures and no render-target changes; the small draw-count difference is not attributable because idle animation/frame timing differed. These scenes therefore do not establish the retail shadow route.
