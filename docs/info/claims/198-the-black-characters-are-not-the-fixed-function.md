---
id: C198
kind: claim
status: holds
created: 2026-08-15
tags: rendering,lighting
---

## Claim

The black characters are NOT the fixed-function lighting arithmetic

## Evidence

X2_LIGHT_SURVEY bounds every draw of every gameplay frame by what the vertex stage can output with N.L forced to 1 and a vertex-coloured material taken as white: 1 of 107800 lit draws over 3256 gameplay frames is bounded black, 0 of them by distance attenuation (scratch/logs/postfix.log). The environment renders correctly against the control. The earlier attenuation reading (C195/C196) came from ONE unrepresentative draw sampled past a per-draw gate.

## What would falsify it

a gameplay frame in which the character draws are shown to be among the bounded-black set, or a survey run whose black count is a large fraction of lit draws
