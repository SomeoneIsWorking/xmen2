---
id: I051
kind: instrument
status: trusted
created: 2026-08-13
---

## Instrument

tools/oracle_probe.py: live guest-state sampling of a control run

## Validated by

Shown BOTH answers before use: --selftest proves it reads a known dword, sees it CHANGE (so it is not caching), returns None rather than a plausible zero for an unmapped address and COUNTS that as a failed read, and that verify() refuses a process with no MZ at the base. Validated against a known answer on real data -- probing our own x2native run reproduced src/native/conversation.c's own counters exactly (conversation live 70.5-72.2s, one cur_line advance, ends at flags 0x18). On the control it reported 0 of 83,716 failed reads, so absent transitions are absence rather than blind spots.

## Known failure modes

(none recorded yet)
