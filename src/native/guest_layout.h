#ifndef GUEST_LAYOUT_H
#define GUEST_LAYOUT_H

/*
 * Where everything lives in the guest's 32-bit address space.
 *
 * These regions were separately chosen in the files that place things, and
 * they have to agree: the game walks upward from just above its image
 * reserving arenas, so the runtime's own memory sits above anything the guest
 * asks for, and a region that quietly overlaps another is found later as a
 * collision with no visible connection to either owner.
 *
 * A host with no VM of its own -- the browser -- backs the whole space with
 * one flat window, so GUEST_LAYOUT_LIMIT is also the size of that window and
 * the reason these regions are packed rather than spread across 4 GB.
 */

#define GUEST_PAGE_SHIFT 12u
#define GUEST_PAGE_SIZE (1u << GUEST_PAGE_SHIFT)

/* The game's own image base, fixed by XMen2.exe. */
#define GUEST_IMAGE_BASE 0x00400000u

/*
 * Sizes are measured need plus headroom, not collision-proof ceilings: the
 * browser commits every byte below GUEST_LAYOUT_LIMIT whether or not it is
 * used (#159). guest_layout_report prints how far a run reached into each
 * region; the figures below are the Dead Zone run that sized them. Each
 * region refuses by name when full rather than spilling into the next.
 *
 * Below GUEST_MODULE_LO: the image, the runtime's fixed low pages, the one
 * DLL that gets its preferred base 0x10000000, and the game's own arenas,
 * which it places by walking VirtualQuery upward from above its image until
 * the GlobalMemoryStatus budget (512 MB) refuses. Measured: they reach
 * 0x08460000.
 */

/* Relocated module images, on the Windows 64 KB allocation granule. The game
   ships 20 DLLs, all but one linked for 0x10000000, so all but one land here.
   Measured: 11.8 MB of images in all. */
#define GUEST_MODULE_LO 0x20000000u
#define GUEST_MODULE_HI 0x22000000u
#define GUEST_MODULE_ALIGN 0x00010000u

/* Where a guest MEM_RESERVE with no address lands. Measured: 2 MB. */
#define GUEST_RESERVE_LO 0x22000000u
#define GUEST_RESERVE_HI 0x26000000u

/* The runtime's own memory: the guest stack and fixed pages, then the heap
   that serves the guest's CRT and Win32 heaps. Measured: the heap's
   high-water is 22 MB. */
#define GUEST_RUNTIME_BASE 0x26000000u
#define GUEST_HEAP_BASE (GUEST_RUNTIME_BASE + 0x01000000u)
#define GUEST_HEAP_SIZE 0x0C000000u
#define GUEST_HEAP_END (GUEST_HEAP_BASE + GUEST_HEAP_SIZE)

/* Mapped file views, above the heap. Views are placed by scanning this range,
   not by a cursor, so unmapping one returns its span. Measured: 16 KB. */
#define GUEST_VIEW_ARENA_BASE GUEST_HEAP_END
#define GUEST_VIEW_ARENA_END (GUEST_VIEW_ARENA_BASE + 0x02000000u)

/* One past the highest address anything above may use: 848 MB. */
#define GUEST_LAYOUT_LIMIT GUEST_VIEW_ARENA_END

#endif
