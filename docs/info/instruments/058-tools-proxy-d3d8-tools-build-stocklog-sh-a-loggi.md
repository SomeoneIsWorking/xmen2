---
id: I058
kind: instrument
status: trusted
created: 2026-08-15
---

## Instrument

tools/proxy_d3d8 + tools/build_stocklog.sh -- a logging d3d8.dll the STOCK game loads under Wine, recording SetLight/LightEnable/SetMaterial and the lighting render states of the ORIGINAL engine

## Validated by

It showed the OTHER answer on the very first run: the port's own instruments said the engine hands over near-black lights (C199), and the control's log says 0 of 139,536 gameplay SetLight calls carry a black diffuse and the engine uses 44 light indices up to 51 where the port held 16. Validated as a pass-through before it was trusted for values: slot 44's generic trampoline disassembles to jmp *0xb0(%ecx) and fwddev_table[44] points at it (0xb0 = 44*4), the trampoline stride is set AND checked by .org so a body that outgrew it cannot assemble, and the build refuses if the proxy exports fewer symbols than the real d3d8, if anything imports d3d8 by ordinal, or if the one implemented export went missing -- which it did on the first attempt, because supplying a .def turns off ld's auto-export. The game ran to gameplay through it by hand, which is the end-to-end proof that the 108 forwarded slots forward correctly.

## Known failure modes

(none recorded yet)
