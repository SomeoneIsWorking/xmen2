---
id: C010
kind: claim
status: falsified
created: 2026-08-04
tags: 
reconfirmed: 2026-08-04
falsified_on: 2026-08-05
---

## Claim

The PC build is the correct recomp target, not the Xbox build, because static x86 recompilation lives or dies on function-boundary discovery and the PC build hands us 50,581 named entry points while the XBE hands us none.

## Evidence

PC: 5,579,457 executable bytes across XMen2.exe + 16 libIG*.dll + libMovie.dll, with 50,581 named function entry points from the export tables (each carrying a full C++ signature in its MSVC mangling) plus 989 named imports. XBE (scratch/xbox_iso/default.xbe, 5,726,208 B): a single statically-linked image, .text 4,049,732 B, 21 sections, NO export table and NO named import table -- only a kernel thunk table importing by ordinal. Second factor: the PC build's GPU boundary is the D3D8 COM API (clean, documented, dxvk-d3d8 exists as reference), whereas the XBE statically links D3D/DSOUND/XGRPH/DOLBY and talks to NV2A at the push-buffer level -- emulating that is the xemu problem. Third: a working differential oracle for the PC build already exists (C005); an Xbox oracle would have to be stood up from scratch.

## What would falsify it

Finding that the PC export tables do not actually cover the code -- e.g. that most of XMen2.exe's own 2.6MB of .text consists of static functions with no exported entry point, leaving boundary discovery just as hard as the Xbox case -- would undercut the main argument. MEASURE the fraction of PC .text reachable from exported/imported entry points before relying on this.

## Re-confirmed 2026-08-04

MEASURED the falsifier's demand -- and it splits. XMen2.exe: 2,611,125 code bytes, ZERO named functions (an exe has no export table), i.e. 47% of the PC code has exactly the Xbox's discovery problem. The 16 engine DLLs: 2,968,332 code bytes with 44,283 named CODE entry points -- roughly one symbol per 67 bytes, effectively full coverage. So the claim SURVIVES but not as originally worded: the PC advantage is that its unsymbolised region (2.61MB) is 36% smaller than the XBE's (4.05MB), AND that the exe's 794 calls into the engine all carry full C++ signatures from the mangling, which constrains its analysis in a way nothing constrains the XBE. The engine is well-conditioned for recomp; XMen2.exe is not, and that is where the game logic lives.

## FALSIFIED 2026-08-05

Wrong on its main premise. C010 argued the Xbox target was infeasible because the XBE statically links D3D/DSOUND and drives NV2A at the push-buffer level, i.e. 'this is the xemu problem'. That cost does not have to be paid: sp00nznet/xboxrecomp is an existing static recompiler for original-Xbox titles that already provides the XBE parser, function identification, x86->C lifter, 115 of 366 kernel ordinals mapped to Win32, D3D8->D3D11 (and an OpenGL backend for Linux), and NV2A plus MCPX audio taken from xemu. It lists Burnout 3 as playable with 22,097 functions lifted and Blood Wake at 99.1% lift success. The 'no symbols in the XBE' half of C010 also matters far less than I claimed, since that project does its own function identification -- exactly as Ghidra did for XMen2.exe, where the symbol-free exe reached the same coverage as the symbol-rich DLLs. What survives is only that the PC work already exists here.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
