---
id: I039
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/d3d8_abi_check.py -- the PC D3D8 interface tables, checked against a real d3d8.h and against the game's own call sites

## Validated by

Validated on BOTH classes, which is what I038 lacked. On the correct table: 239 methods across 11 interfaces agree with /usr/i686-w64-mingw32/.../d3d8.h with 0 disagreements, and 103 of libIGGfx's 204 provably-device call sites give a readable push count that confirms 24 methods exactly, 0 under-counts. On a table deliberately perturbed by one argument at IDirect3DDevice8 slot 50 (--selftest, wired into ctest as d3d8_abi): BOTH checks report the disagreement, the header check by name and the game check at 32 call sites. Its soundness rests on an asymmetry I038 did not draw: an OVER-count has innocent explanations (a callee-saved register pushed on one branch -- libIGGfx 0x10045a5d PUSHes EDI and 0x10045ad4 POPs it -- or an enclosing call's staging), so over-counts are reported as INCONCLUSIVE and never fail the check; an UNDER-count cannot happen, because a call site physically cannot push fewer dwords than the callee pops. Only under-counts fail. Every negative carries its denominator: sites not attributable to the device, sites whose push run could not be read, and the interfaces the header did not declare are each counted and printed. With no header it says which paths it looked in and that it verified NOTHING there, rather than passing quietly.

## Known failure modes

(none recorded yet)
