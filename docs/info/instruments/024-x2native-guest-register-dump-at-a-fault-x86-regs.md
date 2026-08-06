---
id: I024
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

x2native guest register dump at a fault (x86_regs_dump)

## Validated by

Non-uniform by construction and validated on the issue-15 fault: it printed eax 00000000 edx 0000000c esi 00a80004 edi 00000000 -- distinct values, one of which (edi=0) matched the reported fault address (nil) and identified the faulting operand. It also states its own limit in the output: it is the register file of the last body to CROSS THE HOST BOUNDARY, which guest-to-guest calls share (they pass the same CPU* down), so it is live -- but a value a body kept in a C local is not reflected. Says so rather than implying completeness. Reports 'no CPU has crossed the boundary yet' rather than printing zeros if nothing ran.

## Known failure modes

(none recorded yet)
