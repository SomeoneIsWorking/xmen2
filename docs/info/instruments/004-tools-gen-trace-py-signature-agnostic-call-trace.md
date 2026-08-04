---
id: I004
kind: instrument
status: trusted
created: 2026-08-04
---

## Instrument

tools/gen_trace.py -- signature-agnostic call tracer on a DLL export boundary

## Validated by

Produced 9 real call records in boot order from the live game, so it demonstrably fires. Codegen inspected in the shipping artifact (objdump: pusha/pushf/push idx/call/add/popf/popa/jmp *mem) confirming the stack is restored before the jump. Refuses to thunk DATA exports rather than emitting a jump to a vftable -- correctly rejected 2 of the 24 (a vftable and a static _Meta) and said so. Logs first hit plus every 1000th, so a hot symbol cannot bury a cold one's first call. KNOWN GAP: the end-of-run summary listing never-called symbols only runs on DLL_PROCESS_DETACH, which does not happen because the harness SIGKILLs the game -- so 'absent from the log' currently does NOT distinguish never-called from not-yet-called.

## Known failure modes

(none recorded yet)
