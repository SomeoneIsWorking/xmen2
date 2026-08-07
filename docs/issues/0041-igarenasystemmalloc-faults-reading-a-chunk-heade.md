---
id: 41
title: igArenaSystemMalloc faults reading a chunk header at 0x068effcc, past the title screen
status: resolved
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


## Root cause: VirtualQuery and VirtualFree disagreed about what was committed

Not the arena's arithmetic, and not any of C087/C088/C090/C091 -- those were
worth reading first and none of them was this.

`VirtualFree(MEM_DECOMMIT)` mprotects the range `PROT_NONE`, so a
use-after-decommit faults instead of reading stale data. That is right.
`VirtualQuery` reported the whole reservation as **MEM_COMMIT**, justified in
its own comment by "the reservation is mapped PROT_READ|PROT_WRITE" -- which
stopped being true the moment the first decommit ran. The two were consistent
when they were written and one of them changed.

The trace says it in three lines:

    [mem] VirtualQuery(0x068ec000) -> base 0x068e0000 size 65536 state COMMIT
    [mem] VirtualFree(0x068ec000, 16384, type 0x4000)          <- MEM_DECOMMIT
    [mem] VirtualQuery(0x068ec000) -> base 0x068e0000 size 65536 state COMMIT

The fault address, `0x068effcc`, is inside that decommitted 16 KB. The guest
asked, was told the range was committed, used it, and faulted.

**And a decommit was permanent.** `VirtualAlloc` with MEM_COMMIT over a
reservation this host had already mapped took the "already mapped, and the
guest reserved it" path and returned success **without restoring access**, so
the pages stayed `PROT_NONE` and the guest faulted on memory Win32 had just
told it it had.

Both halves fixed, and they now share one record of what is committed:
`VirtualFree(MEM_DECOMMIT)` notes the range, `VirtualAlloc(MEM_COMMIT)` clears
it and mprotects the pages back, and `VirtualQuery` answers MEM_RESERVE +
PAGE_NOACCESS for anything in it -- which is what Windows answers. A decommit
the table cannot hold is COUNTED rather than dropped, because a forgotten
decommit is one VirtualQuery will call committed again.

Past it the run reaches the intro movie and stops on `WINMM.dll!timeSetEvent`
(issue #42). `timeBeginPeriod`/`timeEndPeriod` are implemented -- granted,
because this host's timers are already nanosecond-resolution, which is a real
answer to what the caller asked rather than a stub.
