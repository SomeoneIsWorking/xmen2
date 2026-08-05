---
id: 7
title: git checkout to revert an experiment deleted a previous session's uncommitted work
status: resolved
symptom: The lift dies with AttributeError: 'DisasmEngine' object has no attribute 'decode_from'; a function that clearly existed and is called from functions.py is simply gone. Before noticing, you read a stale generated tree and conclude your change had no effect
tags: workflow,git,vendor,data-loss
created: 2026-08-05
updated: 2026-08-05
---

## What happened

I reverted an experiment with `git checkout tools/disasm/engine.py tools/disasm/disasm.py`, believing I was discarding only my own edits from minutes earlier. `decode_from` was NOT mine -- it was an earlier session's uncommitted work, never committed anywhere -- and the checkout deleted it.

The next lift failed with `AttributeError: 'DisasmEngine' object has no attribute 'decode_from'`. I did not read the lift's log first, so I inspected the generated tree, saw my change had not taken effect, and spent several minutes theorising about caches and translator differences. The tree was simply stale, because the lift had aborted.

## Why it was possible

The parent repo gitignores `vendor/`, and the vendored fork is a separate git repo that nobody had been committing to. So **every** hand-written change in it lived only in the working tree:

| file | uncommitted lines |
|---|---|
| `src/kernel/kernel_bridge.c` | 331 (a whole session of kernel bridges) |
| `src/kernel/xbox_memory_layout.c/h` | 80 |
| `tools/disasm/functions.py` | 15 |
| `tools/recomp/lifter.py` | 40 |
| `tools/recomp/translator.py` | 8 |

Any `git checkout`, `git stash`, or clean in that repo would have taken the lot.

## Fix

Committed `decode_from` (restored verbatim from earlier in the session's terminal output) and then every hand-written file in the fork. Generated output under `src/game/recomp/gen/` is deliberately left dirty -- it is reproducible from the sources plus the XBE.

## Rules this leaves behind

- **The vendored fork is a real repo with real work in it. Commit there too.** Committing the parent alone protects nothing that lives under `vendor/`.
- **Never `git checkout <file>` to undo an experiment in a tree you did not commit first.** Check `git status` for pre-existing modifications; if a file was already dirty when you started, your revert is someone else's data loss. Re-apply the inverse edit by hand instead.
- **Read the log of a background job before reading its output artefacts.** A stale artefact and a fresh one look identical; the exit status does not.
