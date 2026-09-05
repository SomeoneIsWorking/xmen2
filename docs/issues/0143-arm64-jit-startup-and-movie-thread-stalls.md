---
id: 143
title: ARM64 JIT startup and movie-thread stalls
status: open
symptom: cold startup spends most sampled time republishing old code; post-intro frames stall for seconds
tags: performance,jit,arm64,boot,threads
created: 2026-09-05
updated: 2026-09-05
---

## Causes and ownership

JIT translation published the entire accumulated executable-code prefix after
every block. A five-second macOS sample of the old startup found 404 of 414
main-thread samples in `sys_icache_invalidate`. `jit-common` now exposes bounded
range publication; x86port publishes only the bytes just emitted. Failed
translation closes the write window with a zero-byte publication. No persistent
cache or player cache setting is introduced.

After the intro, six libCriMovie workers were still runnable. A second sample
found about 428 of 433 main-thread samples waiting for the guest lock. The
Win32 wait boundary never recognized completed H_THREAD handles, and finite
waits returned timeout after any early timer/condition wake. Thread completion
now stays signaled for every waiter; finite waits recheck the object until their
guest-clock deadline. The cohesive owner is `kernel32_wait.c`, with handle
storage retained in `kernel32.c`.

A separate longjmp defect resumed the most recently reused host-loop local
instead of the selected saved guest continuation. The selected JmpSlot now
restores EIP. The title selftest exercises two checkpoints then longjmps to the
first, checking the continuation, value and stack.

## Observation and verification

All 20 authenticated PC images were mapped on Apple Silicon. Earlier startup
presented only eight frames in roughly 90 seconds; after range publication the
intro presented 1,200 frames by about 14 seconds. These are different live
observations, not a controlled throughput benchmark or menu timing claim.

After the wait fixes, menu frame time was about 16.7 ms (95th percentile about
22.9 ms). The first tutorial run reached 1,000 frames with 428,622,103 translated
block entries, 89,719 compiled blocks, 448,009 compiled instructions and zero
refusals. This bounded run included loading and opening dialogue: it is not a
complete playthrough or independent stock CPU/memory comparison.

The title regression graph ran 132 entries: 131 passed and FMV decode was
skipped without its explicit media fixture. The wait test has 37 checks,
including early wakeups, complete-thread persistence, wait-any/wait-all and
stdcall stack cleanup. The shipping selftest reported zero of 92 failures.
Shared JIT instruction and code-publication tests carry their denominators in
the x86port and jit-common state ledgers.

A later driven tutorial run finished the opening dialogue, observed controls
released, moved the selected hero and camera using the real keyboard binding,
and reached its 20,000-frame bound with 4,511,598,335 JIT block entries,
96,240 translated blocks, 482,006 translated instructions and zero refusals.
A reset 1,073-frame gameplay timing window measured 21.24 ms median and 25.12 ms
95th percentile. Other local game/test processes were competing for CPU; these
numbers establish the observed improvement, not an isolated device benchmark.

The normal `./run.sh` launcher validates 20 images, accepts all five pinned
shared repositories and launches the rebuilt product. Live JIT heartbeat
snapshots prove both zero work at initialization and nonzero work while running
(e.g. 48,591 compiled blocks, 77,273,348 block entries, zero of 48,591 refused
translations). The heartbeat requests this snapshot atomically; only the guest
lock owner reads mutable JIT state. A delayed boundary is reported as pending.
The focused style, structure, source and binary boundary gates pass after this
instrument change.

Local raw observations are retained under `scratch/logs/repair-*` and
`scratch/screenshots/repair-*`; they are gitignored and are not portable evidence
attachments. Full gameplay performance, physical controller behavior, and
stock conformance remain open. Fedora's independent JIT/CI changes have not
been merged into this local work.

## Local integration

The title pins local x86port `cbbb01343a5fa653f4ccb6bc6da4ef294cab10d8` and jit-common
`908febaa671e65d3cb47ce36a88d4ee4434ed6b1` on `codex/mac-jit-startup`. These commits
are available in this workspace and have not been pushed; consolidation must
publish the selected shared commits and set the resulting pins before a remote
clean clone can reproduce this revision. This is a local integration branch,
not a portable release claim.
