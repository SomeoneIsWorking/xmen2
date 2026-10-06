---
id: C188
kind: claim
status: falsified
created: 2026-08-14
tags: controller,xbox,re
falsified_on: 2026-08-20
---

## Claim

Xbox BLACK and WHITE are per-player physical-value sources 8 and 9, copied separately from the logical digital-button mask; pack-use recovery must follow the physical-value reader rather than invent action-mask bits.

## Evidence

Xbox default.xbe: sub_00163240 registers BLACK/WHITE source bytes 8/9; sub_00160D80 returns the source byte and platform code from the record; sub_00163E40 expands analog bytes and passes the 30-float array beside the digital mask; sub_0015FD90 stores both separately; per-player vtable slot +0x10 reads the indexed float.

## What would falsify it

A reference trace showing BLACK or WHITE changing the per-player logical mask without changing physical slots 8/9, or a corrected disassembly proving the record byte is not the analog-source index, falsifies this boundary.

## FALSIFIED 2026-08-20

Xbox sub_00163E40 zeros 30 floats and writes only four axes; Black/White arrive in the separate digital mask, so record bytes 8/9 are not physical-float source indices.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
