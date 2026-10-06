/*
 * The device holds one reference on each object it has bound (issue #38):
 * binding takes it, rebinding the same object keeps it, binding another or
 * NULL gives it back, and a pointer that is no object is refused without
 * touching the binding. Each setter is checked against the refcounts it
 * leaves, so a setter that skips the addref, releases twice, or accepts a
 * bogus pointer because it matches nothing bound comes out as a failure.
 */
#include "d3d8_binding_selftest.h"
#include "../native/x2_log.h"

#include "d3d8_device.h"
#include "d3d8_selftest_call.h"
#include "d3d8_types.h"
#include "guest_heap.h"

#include <stddef.h>
#include <stdint.h>

enum {
  SLOT_SET_TEXTURE = 61,
  SLOT_SET_STREAM_SOURCE = 83,
  SLOT_SET_INDICES = 85,
};

typedef struct BindingCase {
  const char *name;
  int slot;
  int iface;
  uint32_t (*bound)(const D3D8State *state);
} BindingCase;

static uint32_t bound_texture(const D3D8State *state) {
  return state->texture[1];
}

static uint32_t bound_stream(const D3D8State *state) {
  return state->stream[1].guest_ptr;
}

static uint32_t bound_indices(const D3D8State *state) { return state->indices; }

/* SetTexture(stage, t) and SetStreamSource(stream, b, stride) take the
   object second, SetIndices(b, base) first. Stage and stream 1 keep the draw
   selftests' stage and stream 0 out of it. */
static uint32_t bind(D3D8Object *device, const BindingCase *c,
                     uint32_t object) {
  uint32_t args[3];
  if (c->slot == SLOT_SET_INDICES) {
    args[0] = object;
    args[1] = 0;
    return d3d8_selftest_call(device, c->slot, args, 2);
  }
  args[0] = 1;
  args[1] = object;
  args[2] = 16;
  return d3d8_selftest_call(device, c->slot, args,
                            c->slot == SLOT_SET_TEXTURE ? 2 : 3);
}

static int expect(const BindingCase *c, const char *step, int ok) {
  if (!ok)
    x2_log_info("d3d8 binding selftest: FAILED -- %s: %s.\n", c->name, step);
  return ok ? 0 : 1;
}

static int binding_case(D3D8Object *device, const BindingCase *c) {
  const D3D8State *state = d3d8_device_state();
  D3D8Object *a = d3d8_object_new(c->iface, NULL);
  D3D8Object *b = d3d8_object_new(c->iface, NULL);
  const uint32_t ga = d3d8_object_guest(a), gb = d3d8_object_guest(b);
  const uint32_t bogus = guest_malloc(16);
  int fails = 0;

  bind(device, c, 0);
  fails += expect(c, "binding a takes a reference",
                  bind(device, c, ga) == D3D_OK && c->bound(state) == ga &&
                      d3d8_object_refs(a) == 2);
  fails += expect(c, "rebinding a keeps exactly that reference",
                  bind(device, c, ga) == D3D_OK && d3d8_object_refs(a) == 2);
  d3d8_object_release(a);
  fails += expect(c, "rebinding a the guest has released succeeds",
                  bind(device, c, ga) == D3D_OK && c->bound(state) == ga &&
                      d3d8_object_refs(a) == 1);
  fails += expect(c, "a pointer that is no object is refused",
                  bind(device, c, bogus) == D3DERR_INVALIDCALL &&
                      c->bound(state) == ga && d3d8_object_refs(a) == 1);
  fails += expect(c, "binding b gives a's reference back",
                  bind(device, c, gb) == D3D_OK && c->bound(state) == gb &&
                      d3d8_object_refs(a) == 0 && d3d8_object_refs(b) == 2);
  fails += expect(c, "binding NULL gives b's reference back",
                  bind(device, c, 0) == D3D_OK && c->bound(state) == 0 &&
                      d3d8_object_refs(b) == 1);
  d3d8_object_release(b);
  guest_free(bogus);
  return fails;
}

int d3d8_binding_selftest(void) {
  const BindingCase cases[] = {
      {"SetTexture", SLOT_SET_TEXTURE, D3D8_IF_IDirect3DTexture8,
       bound_texture},
      {"SetStreamSource", SLOT_SET_STREAM_SOURCE,
       D3D8_IF_IDirect3DVertexBuffer8, bound_stream},
      {"SetIndices", SLOT_SET_INDICES, D3D8_IF_IDirect3DIndexBuffer8,
       bound_indices},
  };
  D3D8Object *device;
  int fails = 0;

  x2_log_info("\n=== d3d8 binding selftest: device references through the "
              "vtable ===\n");
  d3d8_device_install();
  device = d3d8_object_new(D3D8_IF_IDirect3DDevice8, NULL);
  if (!device) {
    x2_log_info("d3d8 binding selftest: FAILED -- no device object.\n");
    return 1;
  }
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++)
    fails += binding_case(device, &cases[i]);
  if (!fails)
    x2_log_info("d3d8 binding selftest: PASSED -- %u setters hold one "
                "reference per binding\n",
                (unsigned)(sizeof cases / sizeof cases[0]));
  return fails;
}
