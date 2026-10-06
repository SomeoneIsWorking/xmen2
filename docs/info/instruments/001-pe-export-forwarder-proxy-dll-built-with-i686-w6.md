---
id: I001
kind: instrument
status: trusted
created: 2026-08-04
---

## Instrument

PE export-forwarder proxy DLL built with i686-w64-mingw32 + .def (the mechanism for swapping one libIG*.dll at a time while the rest of the game stays original)

## Validated by

Built target.dll forwarding to target_orig.dll with an MSVC-mangled name, a function and a DATA export; ran a linked exe under wine-11.0-staging: printed func(6)=42 data=1234 (correct values through the forwarder). NEGATIVE CONTROL: hid target_orig.dll and the same run aborted with 'Call from ... to unimplemented function target.dll.realfunc' -- so a broken forward is loud, not silent.

## Known failure modes

(none recorded yet)
