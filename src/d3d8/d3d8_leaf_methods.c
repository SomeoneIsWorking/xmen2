/* d3d8_leaf_methods.c -- see d3d8_leaf_methods.h. */
#include "d3d8_leaf_methods.h"

#include "../native/override_leaf.h"
#include "../native/x2_log.h"
#include "d3d8_com.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdlib.h>
#include <string.h>

typedef struct LeafMethods {
  D3D8IfaceId iface;
  const char *const *names;
  int count;
} LeafMethods;

static const char *const kDeviceLeaves[] = {
    "SetTransform",    "GetTransform",         "SetViewport",
    "GetViewport",     "SetMaterial",          "SetLight",
    "LightEnable",     "SetRenderState",       "GetRenderState",
    "SetTexture",      "GetTextureStageState", "SetTextureStageState",
    "DrawPrimitive",   "DrawIndexedPrimitive", "DrawPrimitiveUP",
    "SetVertexShader", "GetVertexShader",      "SetVertexShaderConstant",
    "SetStreamSource", "SetIndices",           "SetPixelShader",
};
static const char *const kBufferLeaves[] = {"Lock", "Unlock"};

#define X2_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const LeafMethods kLeafMethods[] = {
    {D3D8_IF_IDirect3DDevice8, kDeviceLeaves, X2_COUNT(kDeviceLeaves)},
    {D3D8_IF_IDirect3DVertexBuffer8, kBufferLeaves, X2_COUNT(kBufferLeaves)},
    {D3D8_IF_IDirect3DIndexBuffer8, kBufferLeaves, X2_COUNT(kBufferLeaves)},
};

/* The vtable slot's thunk for `name`; a name the interface lacks is a typo in
   the table above, and refused. */
static uint32_t method_thunk(D3D8IfaceId iface, const char *name) {
  const uint32_t vtable = d3d8_iface_vtable(iface);
  const int n = d3d8_iface_method_count(iface);
  for (int k = 0; k < n; k++) {
    const uint32_t thunk = RD32(vtable + (uint32_t)k * 4u);
    const char *module = NULL;
    const char *sym = x86_thunk_name(thunk, &module);
    if (sym && !strcmp(sym, name)) {
      return thunk;
    }
  }
  x2_log_error("d3d8: %s has no method %s to complete in place; not "
               "continuing.\n",
               d3d8_iface_name(iface), name);
  abort();
}

void d3d8_leaf_methods_install(void) {
  for (int i = 0; i < X2_COUNT(kLeafMethods); i++) {
    const LeafMethods *m = &kLeafMethods[i];
    for (int k = 0; k < m->count; k++) {
      x86_register_thunk_leaf(method_thunk(m->iface, m->names[k]));
    }
  }
}
