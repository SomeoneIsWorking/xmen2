---
id: C172
kind: claim
status: holds
created: 2026-08-13
tags: oracle,wine,tooling
---

## Claim

Driving the Wine control's menu needs SPARSE key windows; a dense one leaves it on the difficulty dialog

## Evidence

Four driven control runs, all cached under tools/oracle.py so each is re-checkable. 195-300/12:Return then 380-500/20:Return reaches the tutorial's opening room (keys d31f0f32, c3f255ba). 150-420/6:Return leaves all six samples on the MAIN MENU (key 64b7766d). 195-300/12 alone leaves all eight on the main menu (key 13acab72) -- the second window is what starts the game. 380-960/10:Return leaves all eight on 'Choose a difficulty level' (key f2e36d75). So a press every 10 s or faster defeats the menu rather than driving it, most likely by re-triggering the screen it just left.

## What would falsify it

if a window at 10 s or faster reaches the level in a cached run, or if the 12 s/20 s pair stops reaching it
