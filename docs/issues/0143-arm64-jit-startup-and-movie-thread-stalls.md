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
stock conformance remain open. These Mac observations precede the combined
shared-runtime integration and are not fresh tests of that combined tree.

## Published integration

The exact Mac snapshot is integrated into shared `main`: x86port
`4c23c0b86e09ba6ac47fd57c418f080c1ff6ed76` and jit-common
`03ac795cbc39843e795cb8091fb96bff2b1c9017`, both pinned in `bootstrap.py`.
The merge preserves the Mac startup/store/copy optimizations and the independent
Fedora/Windows ABI and CI contracts. Apple ARM64 keeps the user-approved
binary64 execution behavior while precision metadata still reports its
limitation; full x87 precision is not a prerequisite for this integration.

## Follow-up profiling and optimization

A five-second playable tutorial sample collected 3,691 main-thread samples;
370 top-of-stack samples were in `fesetround`. The portable x87 store path now
avoids changing the host rounding mode when it already matches the guest, and
copies binary64 values directly on hosts whose long double is binary64. No
rounding mode is cached across host calls or guest contexts. All sixteen
host/guest rounding pairs are covered by 341 new checks, passing on ARM64 and
Rosetta x64. Existing x87 and JIT startup tests also pass.

A ten-million-operation leaf benchmark measured f32 stores at 14.63 -> 5.56
ns and f64 at 12.41 -> 3.89 ns. In the subsequent live sample, `fesetround` no
longer appeared among top-of-stack functions with five or more samples. A
reset gameplay timing window reported 16.93 ms median and 19.09 ms 95th
percentile over 1,815 intervals. The scene, sampling and render pacing limit
that observation; it is not evidence that gameplay sped up by the leaf ratio.
The broader binary64 precision limitations remain unchanged.


The next profile identified repeated string-copy work. x86port now uses a
bulk copy for forward REP MOVS when both complete spans are mapped and
disjoint, with no write observer installed. All other cases retain the exact
per-element progress, fault, and observer behavior. The 100,000-operation
4 KB MOVSD benchmark measured 9,482.1 -> 104.0 ns/copy. There are 11,247 passing
copy-state/admission checks and 4,204 passing JIT startup cases on ARM64 and
Rosetta x64; eighteen startup cases specifically execute REP MOVS through
emitted code. Existing shared-suite baseline failures remain documented there.

A fresh tutorial observation reached 7,000 frames. Its last live heartbeat
reported 2,431,451,277 block entries, 94,305 translated blocks, 86,826,712 native
hand-backs and zero refusals of 94,305 translation attempts, with no product
fallback. A reset 2,265-interval window measured 17.75 ms average, 17.78 ms
median and 19.59 ms 95th percentile. This establishes continued operation,
not an FPS improvement over the previous scene. Local captures and profiles
are under `scratch/logs/optimize-copy-*` and
`scratch/screenshots/optimize-copy-after.png`. The title graph again passed
131 tests with its fixture-dependent FMV test skipped; the shipping selftest
passed all 92 checks. Touched shared sources pass formatting and clang-tidy.

The combined integration now pins jit-common main `03ac795`, which merges
the Mac code-publication work with native-host support. The Linux Clang build
passed 132 of 133 CTests after incorporating the copy optimization, including
all 92 shipping native-battery checks and the canonical state validator;
only the unconfigured FMV fixture skipped. A silent, virtual-display
`./run.sh` run at the final pins ended at its
120-frame limit with 165,521,193 JIT block entries, 73,174 translated blocks,
351,768 translated instructions and zero refusals. This establishes the
default launcher and startup boundary, not Linux gameplay or performance.
