/*
 * COM objects the guest can hold, call and release.
 *
 * The engine reaches DirectX through exactly two imports (C108) and then
 * through vtables on the objects those imports return. This file is the second
 * half of that: it makes a HOST object whose vtable the guest can dispatch
 * through, so that libIGGfx's own code -- every igDx8* class, and every helper
 * below their vtables -- runs unmodified and lands here.
 *
 * The split of concerns is deliberate and matches src/vulkan's:
 *
 *   d3d8_abi.h    what the interfaces ARE (slot order, argument counts)
 *   d3d8_com.c    the guest-ABI grubbiness -- vtables, this-pointers, who pops
 *   d3d8_*.c      one interface each, written in plain C values
 *   src/gpu       what actually draws, knowing nothing about the guest
 *
 * An interface method that is not implemented is the EXPECTED state, not an
 * error -- there are 97 on the device alone. What matters is that reaching one
 * says which, by name: "IDirect3DDevice8::SetRenderState (slot 50)" is a work
 * item; "slot 50" is a puzzle.
 */
#ifndef D3D8_COM_H
#define D3D8_COM_H

#include <stdint.h>

#include "d3d8_abi.h"

struct X86pCpu;

typedef enum {
#define D3D8_ENUM(name) D3D8_IF_##name,
  D3D8_INTERFACES(D3D8_ENUM)
#undef D3D8_ENUM
      D3D8_IF_COUNT
} D3D8IfaceId;

typedef struct D3D8Object D3D8Object;

/*
 * A method body.
 *
 * `self` is the object the guest called through, already resolved from the
 * this-pointer on its stack. Read arguments with d3d8_arg(); finish with
 * d3d8_ret(), which pops the count this method's ABI entry declares. An
 * implementation that returns without calling d3d8_ret is caught by name
 * rather than silently shifting the guest stack.
 */
typedef void (*D3D8MethodFn)(D3D8Object *self, struct X86pCpu *C);

/*
 * Install an interface's implementations.
 *
 * `impl` is indexed by slot and may be sparse: a NULL entry keeps the
 * reporting stub. `n` must equal the interface's method count -- passing a
 * shorter array is refused rather than accepted with the tail defaulted,
 * because "the last few methods quietly report" is exactly the failure this
 * whole file exists to make impossible.
 */
void d3d8_iface_implement(D3D8IfaceId id, const D3D8MethodFn *impl, int n);

/* Guest address of the interface's vtable; builds it on first use. */
uint32_t d3d8_iface_vtable(D3D8IfaceId id);

const char *d3d8_iface_name(D3D8IfaceId id);
int d3d8_iface_method_count(D3D8IfaceId id);

/*
 * A new object. `ctx` is whatever the implementing file wants to hang off it
 * and is never interpreted here. Reference count starts at 1, as COM requires.
 *
 * The guest sees a small block of guest-addressable memory whose first word is
 * the vtable pointer. Nothing else about it is documented to the guest, and
 * nothing in the engine reads it -- COM objects are opaque by contract.
 */
D3D8Object *d3d8_object_new(D3D8IfaceId id, void *ctx);
uint32_t d3d8_object_guest(const D3D8Object *o);
void *d3d8_object_ctx(const D3D8Object *o);
D3D8IfaceId d3d8_object_iface(const D3D8Object *o);

/* The object at a guest address, or NULL if that address is not one of ours. */
D3D8Object *d3d8_object_from_guest(uint32_t guest_addr);

/*
 * COM reference counting, kept here rather than in each interface.
 *
 * Every interface needs exactly this, and a per-file counter is how one of
 * them ends up subtly different from the rest. d3d8_object_release returns the
 * new count and runs the destructor when it reaches zero; the object is kept
 * (not freed) so that a guest pointer used after its last Release reports a
 * USE AFTER RELEASE by name instead of landing in reused memory.
 */
long d3d8_object_addref(D3D8Object *o);
long d3d8_object_refs(const D3D8Object *o);
long d3d8_object_release(D3D8Object *o);

/* Called by Release when the count reaches zero, before the object is retired.
   NULL means "nothing to undo". */
void d3d8_object_set_destructor(D3D8Object *o, void (*fn)(D3D8Object *));

/*
 * A SUBRESOURCE: an object whose lifetime is its container's.
 *
 * A texture's mip levels are the case. They are not independently allocated --
 * the level surface is a view onto bytes the texture owns -- so an independent
 * reference count would let the guest release the texture while still holding
 * a level, and the level's pointer would then be into freed memory.
 *
 * So AddRef/Release/refs on an owned object are the CONTAINER's, which is how
 * the container cannot die under a surface the guest is still holding. The
 * owned object's own count is inert and it is excluded from the leak listing,
 * because a permanent count of 1 that nobody can release reads as a leak.
 */
void d3d8_object_set_owner(D3D8Object *o, D3D8Object *owner);
D3D8Object *d3d8_object_owner(const D3D8Object *o);

/* Objects still holding a reference at shutdown, named. A surface the engine
   never released is a leak; one released twice is a bug that would otherwise
   be invisible. */
void d3d8_object_report(void);

/* ---- inside a method --------------------------------------------------- */

/* Argument i, counting from 0 AFTER the this-pointer. */
uint32_t d3d8_arg(struct X86pCpu *C, int i);
float d3d8_argf(struct X86pCpu *C, int i);

/* Finish, returning `hr` in EAX and popping this + the declared arguments. */
void d3d8_ret(struct X86pCpu *C, uint32_t hr);

/* The interface and method currently executing, for diagnostics. */
const char *d3d8_current_method(void);

/* ---- staging ----------------------------------------------------------- */

/*
 * PERMISSIVE MODE -- a staging tool, exactly as src/vulkan's is, and off by
 * default for the same reason: a renderer that silently ignores what it was
 * asked produces output nobody can trust.
 *
 * On, an unimplemented method returns S_OK (or 0 where the ABI returns void or
 * a count) and pops correctly, so the engine can be walked THROUGH the methods
 * that are not written yet. Every skip is counted per method and printed at
 * exit, so a frame produced this way is always read next to the list of what
 * was missing from it.
 */
void d3d8_permissive(int on);

void d3d8_permissive_report(void);

/* Self-test: builds every vtable and dispatches through it, proving the
   reporter fires and that slot N reaches method N. Returns 0 on success. */
int d3d8_com_selftest(void);

#endif /* D3D8_COM_H */
