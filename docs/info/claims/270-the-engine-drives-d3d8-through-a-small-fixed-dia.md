---
id: C270
kind: claim
status: holds
created: 2026-08-26
tags: graphics,d3d8,architecture
depends: src/d3d8/d3d8_report.c#d3d8_host_report
---

## Claim

The engine drives D3D8 through a small fixed dialect: over 400 frames it sets 47 render states of which the draw path reads 13, 14 transforms of which it reads 3, 7 FVFs and one VS 1.1 shader -- so the seam is narrow enough to remove subsystem by subsystem rather than being load-bearing

## Evidence

d3d8_host_report() from 'X2_BOOT_MAP=act0/tutorial/tutorial1 X2_MAX_FRAMES=400 x2native --no-window --d3d8 --run', exit 0, captured in scratch/logs/seam-census.txt: 36354 draws, 1604 clears, 400 presents; 47 render states set with 34 explicitly listed as NOT read by the draw path; 14 transforms set, 11 listed as not read; 7 distinct FVFs (0x142 x11760, 0x112 x16735, 0x42 x4456, 0x152 x1178, 0x102 x1102, 0x144 x338 pre-transformed, 0x12 x13); 1 vertex shader running 772 draws; 101 textures, 434 vertex buffers, 383 index buffers, 784 state blocks. The report names the unread states individually rather than counting them, so the 34 is a list and not an estimate.

## What would falsify it

a run in a different scene -- a cutscene, a menu, or a shader-heavy act -- in which the engine sets render states or transforms outside this set, or uses an eighth FVF or a second vertex shader; the census is one map and generalises only as far as another map confirms it
