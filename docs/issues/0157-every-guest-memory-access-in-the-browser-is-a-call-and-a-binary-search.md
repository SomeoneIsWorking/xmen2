# 0157 — every guest memory access in the browser is a host call and a binary search

- **State items:** S021
- **Status:** measured and localized; the fix is an architecture change, not yet made
- **Follows:** #155 and #156, which removed the costs that were hiding this one

## What the profile says

With the console flood (#155) and the path-resolution round trips (#156) gone,
the browser's Dead Zone route runs at about 2.2 presents per second and the
heartbeat attributes 84% of what it can see to guest bodies. The heartbeat can
only attribute time inside *nested* dispatch spans, so it sees about 1.4 s of
each 5 s interval; a V8 sampling profile covers all of it.

Taken over 15 s with `tools/web_profile.py`, 16 workers. Fifteen of those are
the idle pthread pool and V8 samples them at the same rate as the one doing the
work, so the aggregate says more about the pool size than the program. The
guest worker's own profile — 54,876 samples, **98.6% of them working**:

| frame | share of the guest worker |
|---|---|
| `x86p_sparse_span_access` | **38.61%** |
| `x86p_mem_accessible` | 11.81% |
| `x86p_mem_read_bytes` | 5.33% |
| `x86p_mem_write_bytes` | 2.66% |
| `x86p_wasm_mem_load` | 2.28% |
| `x86p_wasm_mem_ok` | 0.80% |
| `x86p_mem_write` | 0.76% |
| **guest memory access, total** | **~62%** |
| x87 emulation, total | ~12% |
| **translated guest block (category)** | **5.29%** |

The guest's own translated code is 5% of the thread that is supposed to be
running it. Working out *where* its memory operands point is twelve times that.

## Why

In the browser, `X86pMem` uses x86port's sparse mode, and the WASM backend
lowers every guest load and store to a host import call:

```c
uint32_t x86p_wasm_mem_load(const X86pMem *mem, uint32_t address, uint32_t width);
void     x86p_wasm_mem_store(const X86pMem *mem, uint32_t address, uint32_t width, uint32_t value);
int      x86p_wasm_mem_ok(const X86pMem *mem, uint32_t address, uint32_t width, unsigned access);
```

Each of those reaches `x86p_sparse_span_access`, which **binary searches** a
sorted array of mappings. A checked access calls `mem_ok` and then `mem_load`,
so one guest `mov eax, [ebx+4]` costs two import calls and two binary searches.
The array only grows: `change_range` splits a mapping on every protection
change and nothing ever merges them back.

The desktop backends have none of this. `guest_memory.c` reserves one 4 GiB
arena with `mmap`, sets `g_guest_memory_base`, and a guest address is a host
address plus a constant — `X86pMem`'s contiguous mode, which x86port already
supports (`sparse == NULL`). The browser cannot `mmap`, so it took the sparse
path, and the sparse path is what the profile is measuring.

## The fix, and what it costs

Give the browser a flat guest window inside the WASM linear memory, so a guest
access lowers to arithmetic and an `i32.load` instead of a call and a search.

**It fits.** The guest's address footprint in a real browser run is
`0x00080000` (the return trampoline) through the guest stacks at about
`0x70100000` — 1.88 GiB. The link already sets `-sMAXIMUM_MEMORY=4GB`, and V8
reserves WASM32 address space up front while committing pages lazily, so
reserving the window costs address space and not resident memory. The images
themselves are small and dense: `XMen2.exe` at `0x00400000` and the DLLs packed
from `0x10000000` to `0x30000000`.

**What it would lose, and must not.** Sparse mode is how the browser gets
*exact* Win32 page permissions; contiguous mode's comment says plainly that
"native contiguous permissions remain enforced by the host VM", and in a
browser there is no host VM to enforce them. Switching to the existing
contiguous mode as it stands would make a guest write to a read-only page
silently succeed. That is a fidelity regression and is not acceptable as a
performance fix.

So the change is a third mode, not a switch to the second: a contiguous window
**plus** a permission byte per 4 KiB page, also in linear memory. The lowering
then becomes one `i32.load8_u` at `perms + (addr >> 12)` and one `i32.load` at
`base + addr` — two memory accesses in place of two calls and two searches, and
the permission check stays exact.

Work, in order:

1. `shared/x86port`: `X86pMem` gains an optional page-permission table for the
   contiguous mode, with the sparse mode left alone and the desktop path
   unchanged.
2. `shared/x86port` WASM backend: lower loads and stores inline when the model
   is contiguous, baking the window and permission-table bases in as constants
   at translation time. They must never move.
3. `src/native/`: a browser guest-memory owner that reserves the window and
   keeps the title's Win32 page policy in the permission table, replacing
   `guest_memory_sparse.c`'s per-allocation `calloc`.

The shared repo lands first and the consumer's pin moves after, built against
that revision clean, per the shared-repo rule.

## What would falsify this, and the answer

The first reading of this attribution came from a 16-target aggregate in which
fifteen idle workers held 94% of the samples, so the guest worker's share was
an inference. The falsifier stated was: a profile of that worker alone must put
`x86p_sparse_span_access` at roughly a third of it, or the attribution is wrong
and the design above is premature.

It was taken. The worker is 98.6% working and `x86p_sparse_span_access` is
38.61% of it — confirmed, and the table above is now that direct measurement
rather than the inference.

`tools/web_profile.py` gained the per-target breakdown in the same session,
because its own docstring had claimed "a per-worker sample count is itself the
answer to 'which thread is the program'" and that is measurably false: V8
samples an idle worker exactly as often as a busy one. It now ranks targets by
samples that are **not** idle or parked, and prints the busiest one's shares
against its own denominator.
