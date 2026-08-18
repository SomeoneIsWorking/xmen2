---
id: C211
kind: claim
status: holds
created: 2026-08-17
tags: profiler,instrument,performance
---

## Claim

X2_PROFILE (<period-ms>) is a sampling profiler that names a load-window hotspot the existing instruments cannot: it histograms the CURRENT guest body (one store per dispatch) so a body that runs a lot is sampled a lot, where the hotep probe's fixed hash table refuses most of the ~460k distinct entry points the level build dispatches

## Evidence

The hotep probe (X2_HOTEP) returned 0.0 ms bodies in 1-2 dispatches for a 1400 ms load interval: a 64-slot hash (and even 4096 slots) cannot hold 460k distinct EPs, so the hot bodies collided and were refused, and the probe read as "the load is doing nothing" while the wall-time split said 94% guest. X2_PROFILE instead has a sampler thread read `g_sample_ep` (a volatile store on every dispatch, both guest bodies and import stubs) every N ms and accumulates a flat {ep, count} histogram; the report prints at the end of the run through x2_interrupt_reports with the sample total as its denominator. It immediately named the load's top cost: igArenaMemoryPool::isActive at 15.6%, a 2-instruction function reached only by vtable dispatch -- which led directly to the linear-find root cause (C210). The sampler thread stops when x2_report_now is set and the histogram is printed after, so the report reads quiescent state.

## What would falsify it

a run where a body known to execute for the whole interval is not near the top of the histogram (the sample would have to miss it), or where the sampler reports samples while g_sample_ep is never set (a 0-sample report is printed as such, with its denominator)