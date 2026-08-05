---
id: C049
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: tools/xbox_relift.sh, xbox/src/recomp_types.h
---

## Claim

The Xbox lift covered only .text. This XBE has ELEVEN executable sections; D3D, DSOUND, WMADEC, PSFD00/_B, XONLINE, XNET, D3DX, XGRPH and XPP -- 2743 functions, ~430 KB of shipped code -- had zero functions lifted, and the ICALL macro's 'garbage VA' window (>= 0x00400000) discarded every indirect call into them without a word.

## Evidence

summary.json read 'functions_by_section: {.text: 21908}'. Dropping --text-only gives {.text: 21940, D3D: 275, DSOUND: 378, WMADEC: 128, PSFD00: 17, PSFD_B: 4, XONLINE: 610, XNET: 371, D3DX: 119, XGRPH: 613, XPP: 208} = 24,663 functions. The window was found by the ICALL miss tally reporting range-skipped VA 0x0042E52F (XONLINE); replacing it with the image's real code extent (0x00011000-0x0048EF40, read from the section table) took the run from 167 indirect calls to 3644. Afterwards zero indirect calls fail to resolve; the four range-skips left are 0x0, 0x3FFFFFFF, a stack address and 0x0424448B.

## What would falsify it

if any executable section were loaded at a VA outside 0x00011000-0x0048EF40 the bound would discard real code again -- it is read from this XBE's section table, so a different build needs it re-derived
