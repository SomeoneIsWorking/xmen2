/*
 * igVkVisualContext -- what a slot module needs in order to be one.
 *
 * ## Where the file layout comes from
 *
 * The split across gpu_device / igvk_context / igvk_slots_* is taken from
 * DUSKLIGHT (https://github.com/TwilitRealm/dusklight, CC0), a shipping PC
 * port of Twilight Princess built on the zeldaret/tp decomp -- the same
 * architecture as this one, several years further along. Its src/dusk/ is one
 * small .cpp/.h pair per concern with a narrow header each: presentation.hpp
 * is seven lines, gfx.hpp is ten. Nothing accretes into a renderer.cpp.
 *
 * What was taken is that discipline, not code -- theirs is written against the
 * TP decomp's types and an entirely different graphics stack. Applied here it
 * says: the host GPU device is one concern and knows nothing about the guest;
 * the ARK class is another; each group of engine slots is another. The
 * alternative, and what this replaced, was a single igvk_visualcontext.c about
 * to grow 98 slot implementations.
 *
 * Their frame-interpolation design (record-and-replace, guest state never
 * mutated) is the other thing worth reading before this backend grows one, and
 * it is deliberately NOT copied yet: there is no frame to interpolate.
 *
 * ## The shape of the backend, and why it is this shape
 *
 * The engine is already a multi-platform renderer abstraction (C113):
 * igVisualContext is abstract, records its platform implementation in
 * _Meta+0x3c, and on this build that points at igDx8VisualContext. Pointing it
 * at a class of ours means the D3D8 path is never entered rather than
 * reimplemented (C118), and the engine's own code then creates our device
 * (C120).
 *
 * We inherit igDx8VisualContext's whole 334-slot vtable and override only the
 * slots that would call through a Direct3D device this host never made --
 * 98 of them, computed by tools/device_slots.py rather than chosen (C119).
 *
 * ## Super-calling, which is what keeps this honest
 *
 * Most of those 98 are not device code with some bookkeeping attached. They
 * are the reverse. igDxVisualContext::setViewport is 395 instructions of
 * clamping the requested rectangle against the render destination, followed
 * by ONE call to the device. Rewriting that clamp by hand is re-deriving
 * engine behaviour we already have, and any divergence in it would show up as
 * subtly wrong geometry attributed to the Vulkan code.
 *
 * So the default for such a slot is: call the ENGINE's own body first, then
 * read the state it computed out of the object's own fields and program the
 * GPU from that. The engine's bodies are safe to run this way because they
 * guard the device call themselves -- `MOV ECX,[ECX+0x144]; TEST ECX,ECX; JZ`
 * -- and this+0x140/0x144 are deliberately left NULL. Where a body does NOT
 * guard (clearRenderDestination is one), it is implemented directly, reading
 * the same fields the engine already stored.
 *
 * That is igvk_super. A slot that neither super-calls nor explains why it
 * cannot is re-deriving something, and re-derivation is what this design
 * exists to avoid.
 */
#ifndef IGVK_CONTEXT_H
#define IGVK_CONTEXT_H

#include <stdint.h>
#include "igvk_ark.h"
#include "x86rt.h"

/* The module libIGGfx is, spelled once. */
#define IGVK_GFX "libIGGfx.dll"

/* ---- what a slot stub sees ------------------------------------------- */

/* `this`, and the i-th stack argument (0-based, after the return address). */
#define IGVK_SELF(C)     ((C)->ecx)
#define IGVK_ARG(C, i)   RD32((C)->esp + 4u + (uint32_t)(i) * 4u)

/* The i-th stack argument reinterpreted as a float. The engine passes floats
   on the stack as their bit patterns; punning through a union rather than a
   pointer cast keeps this defined. */
float igvk_argf(CPU *C, int i);

/* A float field of the object, by byte offset. */
float igvk_fieldf(uint32_t self, uint32_t off);

/*
 * Run igDx8VisualContext's own body for this slot and return its EAX.
 *
 * `linked_va` is the body's address as libIGGfx was LINKED, which is what
 * tools/device_slots.py reports; it is relocated here. `nargs` is the number
 * of stack arguments, i.e. the body's own `RET N` divided by four -- read out
 * of the binary by device_slots.py, never guessed, because getting it wrong
 * drifts the guest stack and fails somewhere else entirely.
 *
 * This does NOT return from the slot. The caller does that with ark_ret, so
 * that it can act on the result first.
 */
uint32_t igvk_super(CPU *C, uint32_t linked_va, int nargs);

/*
 * The engine's igStatus singletons, as pointer values.
 *
 * libIGGfx returns status by MSVC's hidden-pointer convention: the caller
 * passes an out-slot as the first stack argument, the callee stores a status
 * pointer into it and returns that same out-slot in EAX. The two singletons
 * live behind libIGGfx globals -- the OK one and the failure one, which is how
 * igDxVisualContext::setVideoMode and the Cg loader both spell success and
 * failure.
 *
 * Reading them from the engine rather than inventing a value matters: the
 * caller compares the returned pointer against its own copy of the same
 * singleton, so a fabricated non-NULL reads as an unrecognised failure.
 */
uint32_t igvk_status_ok(void);
uint32_t igvk_status_fail(void);

/* Return an igStatus by that convention. `nargs` counts the out-slot. */
void igvk_ret_status(CPU *C, uint32_t out, uint32_t status, int nargs);

/*
 * Bind a slot. `name` is for diagnostics and for the unimplemented-slot
 * report, and must be the engine's own method name so that the two can be
 * matched up against tools/device_slots.py's listing.
 */
void igvk_slot(int slot, void (*fn)(CPU *), const char *name);

/* ---- the slot modules ------------------------------------------------ */

/*
 * Each module binds the slots it owns. igvk_context.c calls these in order;
 * the list there IS the inventory of what the backend implements, so adding a
 * module means adding one line in one place.
 */
void igvk_install_lifecycle(void);
void igvk_install_frame(void);

/* ---- the class ------------------------------------------------------- */

/* Register and substitute. Returns non-zero when the caller should stop
   retrying -- either installed, or failed for a reason waiting cannot fix. */
int igvk_context_install(void);

/* Arm the substitution on the engine's own first createInstance. */
int igvk_context_arm(void);

#endif /* IGVK_CONTEXT_H */
