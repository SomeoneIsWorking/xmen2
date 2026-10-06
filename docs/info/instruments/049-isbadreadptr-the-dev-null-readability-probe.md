---
id: I049
kind: instrument
status: trusted
created: 2026-08-13
---

## Instrument

IsBadReadPtr / the /dev/null readability probe

## Validated by

FIXED. Both users asked the kernel whether memory was readable by write()ing the range to /dev/null and treating a full-length return as proof. Measured: write(/dev/null, (void*)0x6f6c6e75, 4096) returns 4096 for an unmapped page while process_vm_readv on the same address returns -1 EFAULT. So the probe answered 'readable' for every address -- IsBadReadPtr said 'good pointer' to the game for the whole of every run, and the argument decoder dereferenced wild words and killed its own report. Both now use process_vm_readv, which copies and fails honestly; mem_accessible walks the range in chunks instead of checking only its head. Re-validated on a real run: no fault, and the script launcher's arguments decode to real script text.

## Known failure modes

(none recorded yet)
