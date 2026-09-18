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

/* Relocated module images, 16 MB apart so a module's own sections never share
   a 16 MB block with another's. 32 slots: the game ships 20 DLLs, all but one
   linked for 0x10000000, so all but one are relocated here. Measured with the
   region ending at 0x30000000: the 17th module had nowhere to go. */
#define GUEST_MODULE_LO 0x20000000u
#define GUEST_MODULE_HI 0x40000000u

/* Where a guest MEM_RESERVE with no address lands: between the mapped modules
   below and the runtime's arena above. */
#define GUEST_RESERVE_LO 0x40000000u
#define GUEST_RESERVE_HI 0x6F000000u

/* The runtime's own memory, above everything the guest asks for. */
#define GUEST_RUNTIME_BASE 0x70000000u
#define GUEST_HEAP_BASE (GUEST_RUNTIME_BASE + 0x01000000u)
#define GUEST_HEAP_SIZE 0x20000000u
#define GUEST_HEAP_END (GUEST_HEAP_BASE + GUEST_HEAP_SIZE)

/* Mapped file views, above the heap. Views are placed by scanning this range,
   not by a cursor, so unmapping one returns its span. */
#define GUEST_VIEW_ARENA_BASE 0x92000000u
#define GUEST_VIEW_ARENA_END 0xA0000000u

/* One past the highest address anything above may use. */
#define GUEST_LAYOUT_LIMIT GUEST_VIEW_ARENA_END

#endif
