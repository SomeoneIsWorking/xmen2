---
id: I011
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

Callee-saved contract + simulated-stack bounds checks (xbox/src/recomp_manual.c, on every indirect call)

## Validated by

Both fired on real defects the moment they were added and both now report clean: 'did not restore esi: 0x00560D84 -> 0x00000000' named sub_0027BEF0 directly, and the ESP check named the kernel thunk at 0xFE000124 with 504 of 505 violations. After the fix they read '7642 indirect calls checked, every one restored ebx/esi/edi/ebp' and 'esp stayed inside the guest stack for every checked call' -- the clean case is stated, not silent. Blind spot they name themselves: DIRECT calls are not checked, only indirect. XBOX_ABICHECK=0 disables.

## Known failure modes

(none recorded yet)
