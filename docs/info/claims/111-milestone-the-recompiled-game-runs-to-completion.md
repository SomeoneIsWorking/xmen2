---
id: C111
kind: claim
status: falsified
created: 2026-08-06
tags: pc,recomp,native,milestone,rc-exe
falsified_on: 2026-08-22
---

## Claim

MILESTONE: the recompiled game runs to completion and exits cleanly, with no aborts on any path

## Evidence

x2native --run now returns exit code 0. The recompiled XMen2.exe plus twelve recompiled libIG*/libMovie/libCriMovie modules start up, initialise every module, run the CRT startup and the engine's memory, ARK reflection and registry layers, reach the game's own DirectX 9.0c presence check, report it truthfully as absent, show the dialog, and shut down through their own cleanup path -- releasing memory, closing handles and returning from main without a single abort, unimplemented-import stop, missing body or fault anywhere. 5103 distinct (entry point, module) pairs are entered, up from 2974 at the start of this session and 4958 before the last two fixes. Battery 33/33, ctest 5/5. The last blocker was a missing MSVCRT alias: operator delete and operator delete[] were implemented for the MSVCR71 spelling but never aliased for the MSVCRT one the DLLs import -- so the exe could delete and the DLLs could not.

## What would falsify it

Running to completion is not the same as running the GAME. It exits at the DirectX check, so nothing downstream of graphics has executed: no renderer, no asset loading beyond startup, no gameplay, no audio, no input. The 5103 entry points are the startup and shutdown paths only, and a clean exit here says the host surface is complete enough for those, not that anything else works.

## FALSIFIED 2026-08-22

A retained 2026-08-22 Advanced Options run (PID 2867467) aborted in MSVCR71.dll!??_V@YAXPAX@Z. The generated import expects imp_MSVCR71____V_YAXPAX_Z, but crt.c defined imp_MSVCR71___V_YAXPAX_Z, so the strong implementation never replaced the weak aborting stub. C111 only exercised the earlier startup/shutdown path and its statement that delete[] was implemented was false.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
