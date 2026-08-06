---
id: I027
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

x2native argument watch (X2_ARGS, trace builds)

## Validated by

The native counterpart of the hosted X2_WATCH (I019), which the native build lacked -- the reached set could say how often a function ran but not what it was called with. Validated by producing a coherent, non-uniform history on the issue-15 run: igArena_malloc(0x10)->0x00a80008, igArena_free(0x00a80008), igArena_malloc(0x0c)->0x00a80028, igArena_malloc(0x100)->crash. The values are self-consistent (the freed pointer is exactly what the first malloc returned) which is the check that it is reading real arguments and not stack noise. Negative is reported: if no watched entry point is entered it says so explicitly rather than printing nothing. Stated limit, in its own banner: it cannot know a function's real argument count, so it prints a fixed four stack words and the trailing ones may be the caller's frame. Resolves the function name through the module that HAS the recorded base, never by assuming a preferred address -- the exe is linked for 0x400000, and hardcoding 0x10000000 is exactly the bug that made the boundary ring lie (I026).

## Known failure modes

(none recorded yet)
