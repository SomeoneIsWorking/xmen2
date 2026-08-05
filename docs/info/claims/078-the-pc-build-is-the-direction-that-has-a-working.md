---
id: C078
kind: claim
status: holds
created: 2026-08-05
tags: pc,recomp,direction,strategy
---

## Claim

The PC build is the direction that has a working game to grow into: the real game runs under Wine with a hybrid libIGDisplay.dll in which 156 of 521 functions are our recompiled C and the rest forward to the original. Progress is incremental -- the game keeps working while functions move from 'forwarded' to 'ours' -- which is the property the Xbox front does not have, where nothing works until everything does.

## Evidence

Same session, same harness, three runs: stock game renders the Marvel intro (scratch/screenshots/stock.png); the 156-function hybrid renders it too (scratch/screenshots/recomp.png, game_image_loaded=yes wrapper_alive=yes, 2124 distinct colours); the all-521 build loads and dies ('Unhandled page fault on read access to 000000B4'). tools/build_recomp.sh reproduces all three from one command. Also the reason for the choice: docs/strategy.md's own table -- PC has 50,581 named entry points and a documented D3D8 boundary with a working oracle, Xbox has 0 named entry points and NV2A push buffers, which that doc calls 'the xemu problem'.

CORRECTION (C079, same session): the phrase 'our recompiled C runs in the game' is NOT supported for the 156-function build. X2_WATCH=all shows ZERO recompiled entry points entered during the intro run, with a positive control on the same instrument. What the 156-function build demonstrates is that the proxy/forwarding machinery is transparent; the recompiled bodies are compiled in but not reached on that path. The DIRECTION argument in this claim still stands -- the game runs, the boundary is documented, the loop is incremental -- but the baseline is weaker than stated.

## What would falsify it

if growing the recompiled set turns out to be blocked rather than merely slow -- i.e. if the exclusion list from tools/bisect_recomp.sh stops shrinking the forwarded set, or if a majority of functions must stay forwarded, the 'incremental' claim is not doing the work claimed for it
