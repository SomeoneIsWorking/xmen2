---
id: C054
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: tools/xbox_vtable_seeds.py
---

## Claim

Statically-invisible functions can be harvested in bulk from the XBE's vtables instead of discovered one per run: 1288 real missing functions in one pass, taking the lift from 24,663 to 25,777 functions and clearing every unresolved indirect call the boot path reaches.

## Evidence

tools/xbox_vtable_seeds.py scans data sections for runs of >=3 consecutive dwords pointing into the code range, keeping only those that land in an unclaimed hole: 248,080 words scanned, 43,025 code-shaped, 37,105 in a vtable-shaped run, 1288 new and in a hole (205,055 rejected as not code-shaped, 5,920 as isolated, 35,817 as already-detected or mid-function). Only 2 of the 1288 failed to lift. Before this the runtime loop was finding one address per ~8-minute round; after it, a run reaches 4067+ indirect calls with zero misses and fails on a heap defect instead.

## What would falsify it

the section flags cannot be used to tell code from data in this XBE (.rdata and .data are both marked executable, flags 0x06/0x07), so the tool derives both the code range and the data sections from where the disassembler found functions -- if functions.json is wrong, so is the harvest
