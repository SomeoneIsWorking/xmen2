---
id: 41
title: igArenaSystemMalloc faults reading a chunk header at 0x068effcc, past the title screen
status: open
symptom: SIGSEGV at 0x068effcc in fn_libIGCore_100548a0 (igArenaSystemMalloc), at 10055092 MOV EBP,[EAX] with eax=0x068effcc; reached ~10s into a --d3d8 --run after the title screen renders
tags: pc,native,libIGCore,arena,memory,rc-exe
created: 2026-08-07
updated: 2026-08-07
---

## Where

Only reachable since issues #39 and #40 were fixed. The run renders the title
screen, then:

    *** SIGSEGV at 0x68effcc (not an import slot)
    [REGS] eax 068effcc  ecx 00000034  edx 7101ac10  ebx 7101ac58
    [REGS] esp 700ff200  ebp 00004000  esi 00a9b310  edi 7101ac58

    addr2line -> fn_libIGCore_100548a0 = igArenaSystemMalloc

The instruction is `10055092 MOV EBP,dword ptr [EAX]` -- reading a chunk header
through a pointer the allocator produced. `0x068effcc` is in NO mapped region:
the guest heap starts at 0x71000000 and the images are below 0x01000000, so
this is arithmetic gone wrong, not a live pointer to the wrong thing.

The discovery loop converged on this path in three rounds beforehand, so
nothing between the start and here is a missing body.

## This allocator has form -- read these first

* **C091** the recompiled arena allocator places its top chunk INSIDE a live
  allocation
* **C090** issue #15's NULL is a 16-byte disagreement between `ms->top` and the
  arena's actual top chunk
* **C087 / C088** an UNBOUNDED arena-growth loop inside libIGCore's own
  statically-linked MSVCRT heap

`ecx = 0x34` is the requested size (52 bytes) and `esi = 0x00a9b310` is the
pool. Whether this is the same 16-byte disagreement surfacing at a new call
site, or a fresh defect, is the first question -- and C090/C091 say exactly
which fields to compare.

## The instrument to start with

`X2_ARGS=0x100548a0` gives the pool, the size and the caller for every
allocation, and `X2_PEEK` on the pool's header fields gives the arena's own
view at each one. That pairing is what produced C091's whole time series
(issue #15) -- the fault shows the wreckage; the series shows which call broke
the invariant.
