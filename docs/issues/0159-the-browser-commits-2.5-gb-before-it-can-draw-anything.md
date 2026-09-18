# 0159 — the browser commits 2.5 GB of real memory before it can draw anything

- **State items:** S021
- **Status:** reproduced; cause understood, size not yet reduced
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
high-water marks are what settles it; they are not yet instrumented.
