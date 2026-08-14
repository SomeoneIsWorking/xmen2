---
id: 73
title: A crash that is not a SIGSEGV kills the native run with NOTHING printed
status: resolved
symptom: The game dies during gameplay with a generic 'Illegal instruction'-style message from the shell and no log: no report, no guest registers, no boundary ring. From a log it is indistinguishable from the window being closed.
tags: pc,native,diagnostics,crash,instruments
created: 2026-08-14
updated: 2026-08-14
---

## Root cause

`poison_init` installed a handler for SIGSEGV only -- it exists to name unbound
imports, and every other fatal signal kept its default disposition. SIGILL,
SIGFPE, SIGBUS and SIGTRAP therefore terminated the process before any of this
port's reporting could run. Reported by the user as a crash while moving or
switching characters, with 'no crash log, just a generic C crash, illegal
something'.

## Fix

`src/native/x2native.c`: the SIGSEGV handler became `fault_report` and is
installed for SIGILL/SIGFPE/SIGBUS/SIGTRAP as well. The import-slot analysis
stays SIGSEGV's alone -- `si_addr` means something different for the others,
and reading it as an import slot would invent an explanation. What they share
now prints for all of them: `si_code` decoded into words, the host rip with a
runnable `addr2line` line, the guest registers, and the boundary ring.

SIGABRT is deliberately NOT taken: this port's own aborts already name
themselves on the way out, and exit 134 is what the gates read.

## Proof that it fires

`x2native --fault-selftest` (ctest: `fault_reporter`) raises each signal in a
child with stderr on a pipe and requires the report to NAME that signal, plus a
control child that installs the same handlers, does not fault, and must print
nothing -- so a check that would pass on any output fails. SIGILL is raised
with a real UD2 (`__builtin_trap`), which is the shape of the reported crash;
the other three use `raise()`, which proves the handler and its message but
carries `si_code` SI_USER rather than a hardware code, and the selftest says
so rather than implying otherwise.

## What this does NOT do

It does not fix the crash. It makes the next occurrence name itself: which
signal, which host body (via addr2line), and what the guest registers and the
last boundary crossings were.
