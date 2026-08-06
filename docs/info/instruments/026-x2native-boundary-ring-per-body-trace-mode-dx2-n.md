---
id: I026
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

x2native boundary ring, per-body trace mode (-DX2_NATIVE_TRACE=ON)

## Validated by

CAUGHT LYING AND FIXED, 2026-08-06. The ring records two different address spaces: host-side crossings note a MAPPED address, while the per-body enter/exit hook is called from generated code that knows only its own LINKED entry point. The dump decoded every entry as mapped, so it confidently attributed libIGCore functions to libIGUtils ('libIGUtils.dll!0x1003c420') and reported others as 'no registered module'. Wrong-module attribution is worse than none: it reads as an answer. Fixed by recording the module's runtime base alongside the entry point (X86_IMGBASE is already available in every generated TU) and resolving linked eps by base. VALIDATED after the fix on the issue-15 run: zero entries now report 'no registered module' or an unresolvable base, where before the tail of the ring was mostly misattributed, and every line resolves to libIGCore with a plausible name (igArena_malloc, consolidate, enterAndLock, memoryOperation). Remaining honest limit: for an enter/exit entry esp_in == esp_out, so the (+0) delta column is meaningless on those rows -- an imbalance shows as a difference between a function's OWN enter and exit esp, not in that column.

## Known failure modes

(none recorded yet)
