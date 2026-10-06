/* d3d8_device_bindings.c -- see d3d8_device_bindings.h. */
#include "d3d8_device_bindings.h"
#include "../native/x2_log.h"

#include "d3d8_device.h"
#include "d3d8_types.h"
#include "x86rt.h"

#include <stddef.h>

/*
 * The device holds a REFERENCE on whatever is bound to it.
 *
 * D3D8 does, and it is not bookkeeping: the engine creates an index buffer per
 * mesh, binds it, draws, and releases it, expecting the device's own reference
 * to keep it alive until something else is bound. Without that reference the
 * release takes the object to zero, this host retires it and destroys its GPU
 * buffer -- and the still-bound guest pointer then resolves to a RECYCLED gpu
 * slot holding somebody else's, smaller, buffer. That is issue #38: a draw
 * asking for 204 indices out of a 152-byte buffer, one per frame, which was
 * the game's missing caption.
 *
 * Order matters: addref the new one BEFORE releasing the old, or binding a
 * resource to itself frees it.
 */
static void bind_ref(uint32_t *slot, uint32_t next, D3D8Object *next_obj) {
  D3D8Object *o;
  if (*slot == next)
    return;
  if (next_obj)
    d3d8_object_addref(next_obj);
  if (*slot && (o = d3d8_object_from_guest(*slot)) != NULL)
    d3d8_object_release(o);
  *slot = next;
}

/* The object a setter is about to bind, resolved only when it differs from
   what the slot already holds: rebinding the bound object -- most of the
   engine's calls -- is then a compare, not two reads of cold guest memory.
   NULL for a pointer that is no object, which the setter refuses. */
static D3D8Object *bound_candidate(const uint32_t *slot, uint32_t next) {
  return next && next != *slot ? d3d8_object_from_guest(next) : NULL;
}

/* bind_ref for a binding that has not been resolved yet. */
static void bind_ref_guest(uint32_t *slot, uint32_t next) {
  bind_ref(slot, next, bound_candidate(slot, next));
}

void d3d8_bound_objects_snapshot(D3D8BoundObjects *out,
                                 const D3D8State *state) {
  out->indices = state->indices;
  for (unsigned i = 0; i < D3D8_MAX_STREAMS; i++)
    out->stream[i] = state->stream[i].guest_ptr;
  for (unsigned i = 0; i < D3D8_MAX_STAGES; i++)
    out->texture[i] = state->texture[i];
}

/* bind_ref from the binding `before` held to the one `slot` holds now. */
static void follow(uint32_t *slot, uint32_t before) {
  const uint32_t next = *slot;
  *slot = before;
  bind_ref_guest(slot, next);
}

void d3d8_bound_objects_follow(D3D8State *state,
                               const D3D8BoundObjects *before) {
  follow(&state->indices, before->indices);
  for (unsigned i = 0; i < D3D8_MAX_STREAMS; i++)
    follow(&state->stream[i].guest_ptr, before->stream[i]);
  for (unsigned i = 0; i < D3D8_MAX_STAGES; i++)
    follow(&state->texture[i], before->texture[i]);
}

void d3d8_dev_SetTexture(D3D8Object *self, CPU *C) {
  D3D8State *state = d3d8_device_state();
  uint32_t stage = d3d8_arg(C, 0), tex = d3d8_arg(C, 1);
  (void)self;
  if (stage >= D3D8_MAX_STAGES) {
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  D3D8Object *obj = bound_candidate(&state->texture[stage], tex);
  if (tex && tex != state->texture[stage] && !obj) {
    x2_log_error("d3d8: SetTexture(%u, 0x%08x) -- that is not a texture "
                 "this host made.\n",
                 stage, tex);
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  bind_ref(&state->texture[stage], tex, obj);
  d3d8_ret(C, D3D_OK);
}

void d3d8_dev_SetStreamSource(D3D8Object *self, CPU *C) {
  D3D8State *state = d3d8_device_state();
  uint32_t stream = d3d8_arg(C, 0), buf = d3d8_arg(C, 1);
  uint32_t stride = d3d8_arg(C, 2);
  (void)self;
  if (stream >= D3D8_MAX_STREAMS) {
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  D3D8Object *obj = bound_candidate(&state->stream[stream].guest_ptr, buf);
  if (buf && buf != state->stream[stream].guest_ptr && !obj) {
    x2_log_error("d3d8: SetStreamSource was given 0x%08x, which is not "
                 "a buffer this host made.\n",
                 buf);
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  bind_ref(&state->stream[stream].guest_ptr, buf, obj);
  state->stream[stream].stride = stride;
  d3d8_ret(C, D3D_OK);
}

void d3d8_dev_SetIndices(D3D8Object *self, CPU *C) {
  D3D8State *state = d3d8_device_state();
  uint32_t buf = d3d8_arg(C, 0), base = d3d8_arg(C, 1);
  (void)self;
  D3D8Object *obj = bound_candidate(&state->indices, buf);
  if (buf && buf != state->indices && !obj) {
    x2_log_error("d3d8: SetIndices was given 0x%08x, which is not a "
                 "buffer this host made.\n",
                 buf);
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  bind_ref(&state->indices, buf, obj);
  state->base_vertex_index = base;
  d3d8_ret(C, D3D_OK);
}
