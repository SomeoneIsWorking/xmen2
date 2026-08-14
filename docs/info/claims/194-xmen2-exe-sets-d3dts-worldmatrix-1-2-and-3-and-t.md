---
id: C194
kind: claim
status: holds
created: 2026-08-14
tags: graphics,d3d8,native
---

## Claim

XMen2.exe sets D3DTS_WORLDMATRIX(1), (2) and (3) and the eight texture transforms D3DTS #16..#23, and this backend reads NONE of them -- it reads WORLD, VIEW and PROJECTION only, so 11 of the 14 transforms the engine sets are dropped. It also sets 47 render states of which the draw path reads 13. D3DRS_VERTEXBLEND and D3DRS_INDEXEDVERTEXBLENDENABLE were NOT among the states set in this run, so the extra world matrices are a LEAD for the reported model warping, not a demonstrated cause: without a blend weight D3D8 itself would ignore them too.

## Evidence

The state report now ENUMERATES rather than intersecting a hand-written list: for every state the engine set it asks d3d8_drawcall_reads_state(), and prints the ones that are not read, naming what it can and printing the number for the rest. scratch/logs/lv.log, a 6000-frame gated tutorial run: '13 of the 47 render state(s) ... are read; 34 are not' and '3 of the 14 transform(s) ... are read (WORLD, VIEW, PROJECTION); 11 are not', with D3DTS_WORLDMATRIX(1..3) and D3DTS #16..#23 listed by name.

## What would falsify it

a run in which D3DRS_VERTEXBLEND or D3DRS_INDEXEDVERTEXBLENDENABLE is set to a non-zero value -- that would turn the dropped world matrices from a lead into the cause of warped skinning
