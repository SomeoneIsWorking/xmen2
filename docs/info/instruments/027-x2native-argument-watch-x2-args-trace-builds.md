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

**Fixed 2026-08-07 -- it printed pointers, not the strings behind them.** Most
arguments worth watching are `char*`, and the watch showed only the hex word, so
"which library failed to load" was unanswerable from a run that had already
ended. It now decodes each of ECX and the four stack words as a string when the
bytes are printable, and validated on the cg.dll stop (C1xx / issue #45): it
recovered both the library name (`"cg.dll"`) and the report handler's format
string (`"Library %s could not be loaded..."`) from libIGCore.

Its own first version had this instrument's characteristic failure and is worth
recording: it bounded the read to mapped module images and live guest-heap
blocks, which was *safe* but printed NOTHING for the one argument the run was
about -- a string on the game's own CRT heap, which this host does not track
block by block. A silently-narrow reader read exactly like "that word is not a
string". It now probes readability with a `write()` to /dev/null, so the kernel
answers EFAULT for unmapped memory instead of the process taking SIGSEGV inside
the diagnostic, and there is no blind spot to be silent about.

Remaining limit, stated: a printable run that fills the 120-byte buffer or runs
off the end of a mapped page is printed TRUNCATED with a trailing `...`, and
short printable byte-runs in a struct can still decode as text (e.g. a stack
out-parameter printed as `"0W"`). Every such line carries the address it came
from, so a spurious one is identifiable rather than merely plausible.

**Two more, found 2026-08-11 chasing the movie stall (issue #50).**

*An entry with no matching exit does NOT mean the body is still running.* It
usually means the body returned through a TAIL CALL, so `X86_EXIT_FN` fired
with the CALLEE's entry point and the watch never saw the exit. libCriMovie's
functions do this constantly. Reading that silence as "still inside" produced
three wrong diagnoses in a row -- `FUN_10002a70 never returns`, then
`FUN_10002d60 never returns`, both false -- before `gdb -p` on the live process
settled where the threads actually were. This is a NATIVE ELF: a real debugger
works on it, and reaching for one earlier beats another round of static
reading.

*A bare address was ambiguous across modules.* Every `libIG*.dll`, `libCriMovie`
and `libMovie` is linked for 0x10000000, so `X2_ARGS=0x100026f0` matched
libIGSg's `igMatrixObjectPool::getClassMeta` for a run that was asking about
libCriMovie's movie init -- and nothing in the output said which module it had
picked. FIXED: the watch takes `module:0xADDR` (`libCriMovie:0x100026f0`), and
prints at startup which module each watched address will match, or says
explicitly that an unqualified one will match ANY.

