---
id: I047
kind: instrument
status: trusted
created: 2026-08-13
---

## Instrument

X2_ARGS (native entry-point argument watch)

## Validated by

It is a TRACE-BUILD instrument and in an ordinary build it printed NOTHING -- no banner, no report, no refusal -- because both the watch hook and its report sit inside #ifdef X86_NATIVE_TRACE. A run watching 0x004a11c0 (the script-path builder) came back silent and that silence was about to be read as 'the function is never entered'. It now says at STARTUP which case it is, and names the reconfigure line; verified both ways on real runs -- the refusal prints when X2_ARGS is set in a non-trace build, and nothing at all is printed when it is unset.

## Known failure modes

(none recorded yet)
