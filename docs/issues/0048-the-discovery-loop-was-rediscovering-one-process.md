---
id: 48
title: The discovery loop was rediscovering, one process launch at a time, what the linker had already written down
status: resolved
symptom: native_discover.sh reports '1 missing target in <module>' round after round, each costing a Ghidra re-analysis, a re-emit and a relink
tags: tooling,lift,discovery-loop,rc-lift,workflow,root-cause
created: 2026-08-11
updated: 2026-08-11
---

## The complaint, which was right

msdia80 gave exactly one missing target per round for eight rounds and was
still going. libIGGfx did the same, and libIGOpt did eleven. Each round is a
full Ghidra re-analysis, a re-emit and a relink -- two to four minutes -- to
learn ONE function.

## Why the existing bulk seeders did not stop it

Both of them GUESS, and both under-approximate on purpose:

* `SeedPointerTables.py` requires **three consecutive aligned dwords** in
  read-only data, because that is what a vtable looks like and what a stray
  constant does not. A single function pointer in a struct is invisible to it,
  and `.data` needs `SEED_SCAN_DATA=1`.
* `seed_code_imms.py` reads immediates out of instruction text, so it finds a
  callback handed to a registrar and nothing that lives in data at all.

## Root cause

The information was never missing. **Every absolute address baked into a
relocatable image has a base relocation entry**, because the loader has to fix
it up if the image moves -- so the set of relocation values that land in an
executable section IS every absolute code pointer in the module. No run-length
threshold, no alignment assumption, no `.rdata`/`.data` distinction, no
heuristic at all.

Measured: both targets the loop found in msdia80 one round at a time
(`0x103f06fa`, `0x103f0715`) were already sitting in `.reloc`.

## The fix

`tools/seed_relocs.py` reads the relocation table and emits every value that
points into an executable section and is not already covered by a known
function. Wired into `add_module.sh` (before the code-immediate pass) and into
`native_discover.sh`'s bulk stage.

One pass on msdia80: **3442 -> 4824 functions**, 149k -> 169k instructions.
The run then cleared msdia80 completely and returned to the frontier it had
before msdia80 joined the build. Ten-plus rounds, replaced by one step.

A relocation value landing in `.text` is not PROOF of code -- MSVC puts
read-only tables and string literals in `.text` for a DLL with no `.rdata`
section, and a pointer to one relocates exactly like a function pointer. That
call belongs to Ghidra's code/data separation, so `AddFunctions.py` now refuses
an address inside DEFINED DATA and counts what it refused: of msdia80's 3071
candidates, 1267 became functions, 1587 were rejected as data, 62 did not
disassemble.

## What this does NOT cover, so the loop stays

* a target computed at run time -- `base + index`, an RVA table, a switch's own
  jump table. There is no absolute address in the file, so the linker never
  recorded one.
* an EXE linked `/FIXED`, which has no relocation directory at all.
  `seed_relocs.py` REFUSES for one rather than reporting nothing found.

The discovery loop is now the CHECK that the static pass was complete, not the
mechanism for finding targets. C149.

## Found on the way: the step guard lied

The seeding pass was refused with "this step produced NO '^ADD:' output of its
own" -- after creating 1267 functions. `run_step`'s guard was
`printf ... | grep -q`, and `grep -q` exits at the first match and closes the
pipe; under `set -o pipefail` the writer's SIGPIPE becomes the pipeline's
status. The guard against a step that did nothing fired on the step that did
the most, and nothing in the message hinted at volume. I040, fixed, with a
20k-line case in `ghidra_export.sh --selftest` that the old code fails.

### Note (2026-08-11)
VERIFIED tree-wide: one native_discover.sh run with relocation seeding in the bulk stage seeded all 20 exported modules and round 1 then found NO missing constructor targets anywhere -- the run now stops on a missing import (KERNEL32!SuspendThread) rather than a missing body. XMen2.exe behaved as documented: no relocation directory (/FIXED), so the tool refused for it and said it had searched nothing.
