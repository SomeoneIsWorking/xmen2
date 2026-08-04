---
id: C002
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

libIGDisplay.dll can be replaced in isolation by satisfying only 24 unique symbols program-wide, making it the correct first native DLL swap - and it is exactly where the three target features (controller hotswap, auto-mapping, Xbox prompts) live.

## Evidence

Union of inbound imports of libIGDisplay across XMen2.exe + all libIG*.dll + libMovie.dll = 24 unique mangled symbols (exe 9, Gui 10, Viewer 7, Insight 4, Audio 2, Movie 2, deduped). Next smallest replaceable DLL is libIGOpt at 18 but nothing imports it from the exe; libIGUtils is 75, libIGCore 784.

## What would falsify it

A runtime LoadLibrary/GetProcAddress on libIGDisplay, or a plugin in plugins/ importing it, would add symbols beyond the static 24.
