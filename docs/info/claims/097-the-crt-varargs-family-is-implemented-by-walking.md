---
id: C097
kind: claim
status: holds
created: 2026-08-06
tags: pc,jit,native,host,crt
---

## Claim

The CRT varargs family is implemented by walking the guest stack

## Evidence

A va_list on x86-32 cdecl IS a pointer into the guest stack -- arguments right-to-left, each padded to 4 bytes, doubles taking 8 -- so printf/vprintf/sprintf/vsprintf/_snprintf/_vsnprintf are implemented by a format walker that pulls each argument from guest memory and formats one directive at a time with the host's snprintf. MSVC's _snprintf truncation semantics (-1 on overflow, no NUL) differ from C99 and are applied explicitly rather than left as a mismatch. An unimplemented conversion REFUSES by name instead of emitting something plausible, because the output becomes a path or a key the game then uses. Covered by tests/test_vformat.c, 11 known-answer checks. Also implemented this round: GetModuleFileNameA from the module table, Get/SetPriorityClass and Get/SetThreadPriority (stored and returned -- this host does not reschedule, and reporting a value the caller never set would be the lie), GetProcessTimes/GetThreadTimes from CLOCK_PROCESS_CPUTIME_ID, and FormatMessageA rendering the numeric ID rather than inventing prose. MEASURED: distinct (entry point, module) pairs entered 2044 -> 2121; battery 33/33; ctest 5/5.

## What would falsify it

The walker is tested against a stand-in argument array, not against the game's own calls -- no run has yet been checked for a format directive the game uses that the walker refuses. The refusal is loud, so such a case would stop rather than corrupt, but 'covers what the game needs' is not established.
