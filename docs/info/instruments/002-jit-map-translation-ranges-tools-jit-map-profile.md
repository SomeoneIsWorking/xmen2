---
id: I002
kind: instrument
status: trusted
created: 2026-09-23
---

## Instrument

jit.map translation ranges + tools/jit_map_profile.py (perf samples charged to guest blocks, named by nearest export)

## Validated by

x86port test_jit_engine 'translate_watch_names_each_published_block' (two blocks told once each; off after NULL; killed by removing the watch call). jit_map_profile --selftest: a flush-reused host range evicts the older block, unowned samples counted, export/module naming; killed by removing eviction. In-game Dead Zone: 59.6% of samples in 2539 blocks, non-uniform (top igFrustCullTraversal::computeCompositeMatrix 5.05%). It replaces nearest-FAULTPC-marker attribution, which charged 26.7% of JIT to one 64-byte guest region that owned only stubs and register write-backs.

## Known failure modes

(none recorded yet)
