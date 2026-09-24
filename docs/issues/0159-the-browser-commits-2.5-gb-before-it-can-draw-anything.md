---
id: 159
title: the browser commits 2.5 GB of real memory before it can draw anything
status: resolved
symptom: the flat guest window commits the whole 2.5 GB up front, so the product refuses to start where the host will not hand it over
state_items: S021
tags: web,browser,wasm,memory,startup
created: 2026-09-19
updated: 2026-09-24
---

# 0159 — the browser commits 2.5 GB of real memory before it can draw anything

- **State items:** S021
- **Status:** resolved: the window is 848 MB, measured and packed
- **Caused by:** the flat guest window (#157), which is otherwise the reason
  the browser got four times faster

## What happens

The browser product refuses to start when the host will not hand it 2.5 GB:

```
growMemory: Attempted to grow heap from 67108864 bytes to 2719350784 bytes,
  but got error: RangeError: WebAssembly.Memory.grow(): Unable to grow instance memory
Failed to grow the heap from 67108864 bytes to 2719350784 bytes, not enough memory!
[x2:error] guest_memory: cannot reserve the 2560 MB guest window;
  the packed layout in guest_layout.h needs it contiguous
```

The refusal itself is right: it names the size, names the reason, and stops.
What is wrong is the size. Measured on this machine with 15 GB of RAM, 8 GB
already in use and 12 GB of swap already taken by other work, the allocation
failed; with the same build and a freshly started browser it succeeded. A
player with a browser, a chat client and a couple of tabs is on the same edge.

## Why it is 2.5 GB

`src/native/guest_layout.h` places the whole guest address space and the window
must cover it contiguously, because x86port's generated code bakes the window
base in as a constant and reaches an operand with one add. The layout limit is
`GUEST_VIEW_ARENA_END`, 0xA0000000, so the window is 2.5 GiB:

| region | range | size |
|---|---|---|
| host objects and the image | 0x00000000–0x20000000 | 512 MB |
| relocated modules, 32 slots of 16 MB | 0x20000000–0x40000000 | 512 MB |
| guest MEM_RESERVE arena | 0x40000000–0x6F000000 | 752 MB |
| runtime base and heap | 0x70000000–0x91000000 | 528 MB |
| file-view arena | 0x92000000–0xA0000000 | 224 MB |

`-sMAXIMUM_MEMORY=4GB` means V8 reserves the address space up front, so the
failure is not address space: it is commit. Every byte of the window is
committed the moment it is allocated, including regions the run may never
touch. #157 measured the address footprint of a real browser run at 1.88 GiB,
and that is the span between the lowest and highest address used, not the
amount used — the holes inside it are committed too.

## What would fix it, and what would not

**Not** dropping back to sparse mode: that is #157 and it cost 62% of the guest
worker.

The sizes above are ceilings chosen so regions could not collide, not measured
requirements. The work is to measure what a real run actually needs of each —
how many module slots the title fills, how much the guest ever reserves, how
much of the heap and the view arena is ever live — and pack the layout to that,
with each region refusing by name when it is exhausted rather than silently
overrunning its neighbour. A region that must stay large can sit above the
others so the window can end below it.

## What would falsify the diagnosis

If a run that reaches gameplay is measured to have live mappings across the
whole 2.5 GiB rather than inside a much smaller working set, the layout is not
over-provisioned and the size is the price of the design. The per-region
high-water marks are what settles it.

## Measured, then packed (2026-09-24)

`guest_layout_report` now prints, with the shutdown reports, how far a run
ever reached into each region, from a "mapped before" byte per page that
`guest_memory.c` already kept for zeroing reused pages. A native Dead Zone run
(`X2_BOOT_MAP=act1/deadzone/deadzone1`, 90 s in the level):

| region | size | reached | ever mapped |
|---|---|---|---|
| image, runtime low pages, the game's arenas | 512 MB | 260 MB | 129 MB |
| relocated modules | 512 MB | 256 MB (16 MB stride) | 11.8 MB |
| guest MEM_RESERVE arena | 752 MB | 2 MB | 2 MB |
| runtime (stack) | 16 MB | 1 MB | 1 MB |
| heap | 512 MB | high-water 22 MB | mapped whole |
| file views | 224 MB | 16 KB | 16 KB |

So the diagnosis held: the working set is far inside the span. The image
region is the one that stays: the game places its arenas by walking
VirtualQuery upward from its image until the 512 MB GlobalMemoryStatus budget
refuses (it reached 0x08460000 here), and one DLL keeps its preferred base at
0x10000000.

The new layout gives modules 32 MB on the 64 KB Windows allocation granule
(the 16 MB step was only the search stride of the original mmap loop; every
module base is resolved at runtime through `x86_module_base`), the reservation
arena 64 MB, the heap 192 MB and views 32 MB. `GUEST_LAYOUT_LIMIT` is
0x35000000, 848 MB.

Packing needed one fix first. VirtualQuery called any page it could not
attribute to a module, a guest reservation or the heap FREE, including the
runtime's own stack and TIB; that is how the game's arena walk once ran into
the stack at 0x30000000, and why the runtime had been kept at 0x70000000. The
virtual-memory imports moved out of `kernel32.c` into `kernel32_virtual.c`
(the legacy ratchet went from 3624 to 3087 lines). Their free spans now come
from the page table (`guest_memory_run`): a mapped page nobody else claims
reads MEM_RESERVE with no access, and nothing at or above the layout is ever
offered. `GetSystemInfo` reports `GUEST_LAYOUT_LIMIT - 1` as the maximum
application address to match.

After: the same native run reaches modules 12.2 MB, reserve 2 MB, heap
high-water 22 MB (11% of 192 MB), nothing above the layout, and no refusals.
The `cutscene-skip` (11/11) and `deadzone-render` (6/6) live cases pass. In
Chrome with WebGPU, `#test-play` runs with 1,029,177,344 bytes of wasm memory
(was a 2,719,350,784-byte grow), presents 293 frames per 5 s (the vsync cap, as
before), and refuses 0 draws.
