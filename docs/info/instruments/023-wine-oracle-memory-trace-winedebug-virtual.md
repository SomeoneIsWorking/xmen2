---
id: I023
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

Wine oracle memory trace (WINEDEBUG=+virtual)

## Validated by

Environment-only, so the game's own Wine prefix registry is never modified -- the relay-filter alternative would have required writing HKCU\Software\Wine\Debug. Validated by producing a NON-uniform answer that discriminated: 126 NtAllocateVirtualMemory calls of which exactly 4 were large arena reservations (19.6/19.5/16.0/21.5 MB), against the native build's 67 calls and ~527 MB. A trace that reported 'lots of allocations' in both would have proved nothing; this one separated the two classes and settled C087's falsifier.

## Known failure modes

(none recorded yet)
