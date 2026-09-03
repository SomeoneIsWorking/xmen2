#include "d3d8_selector_probe.h"

#include "gpu_device.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <string.h>

static uint32_t g_world_matrix_source;
static uint32_t g_world_matrix_guest;
static uint32_t g_world_matrix_set_caller;
static uint32_t g_world_matrix_set_source;
static int g_world_matrix_set_found;
static uint32_t g_world_matrix_multiply_caller;
static uint32_t g_world_matrix_multiply_left;
static uint32_t g_world_matrix_multiply_right;
static int g_world_matrix_multiply_found;
static float g_world_matrix_multiply_left_value[16];
static float g_world_matrix_multiply_right_value[16];
static int g_world_matrix_multiply_inputs_readable;
static D3D8SelectorMultiplyStep
    g_world_matrix_multiply_chain[D3D8_SELECTOR_MULTIPLY_CHAIN_MAX];
static uint32_t g_world_matrix_multiply_chain_count;
static int g_world_matrix_multiply_chain_truncated;

enum { MATRIX_SET_HISTORY_CAPACITY = 1024 };

typedef struct {
  uint32_t attr;
  uint32_t caller;
  uint32_t source;
} MatrixSetRecord;

static MatrixSetRecord g_matrix_set_history[MATRIX_SET_HISTORY_CAPACITY];
static uint32_t g_matrix_set_count;

typedef struct {
  uint32_t output;
  uint32_t caller;
  uint32_t left;
  uint32_t right;
  float left_value[16];
  float right_value[16];
  int inputs_readable;
} MatrixMultiplyRecord;

static MatrixMultiplyRecord
    g_matrix_multiply_history[MATRIX_SET_HISTORY_CAPACITY];
static uint32_t g_matrix_multiply_count;

typedef struct {
  uint32_t destination;
  uint32_t caller;
  uint32_t source;
  float source_value[16];
  int source_readable;
} MatrixCopyRecord;

static MatrixCopyRecord g_matrix_copy_history[MATRIX_SET_HISTORY_CAPACITY];
static uint32_t g_matrix_copy_count;

typedef struct {
  uint32_t destination;
  uint32_t caller;
  uint32_t source;
  uint32_t title_builder_caller;
  uint32_t title_builder_this;
  uint32_t title_builder_translation;
  uint32_t title_builder_rotation;
  float title_builder_scale[3];
  int title_builder_found;
} TransformSetRecord;

static TransformSetRecord g_transform_set_history[MATRIX_SET_HISTORY_CAPACITY];
static uint32_t g_transform_set_count;

static struct {
  uint32_t caller;
  uint32_t object;
  uint32_t translation;
  uint32_t rotation;
  float scale[3];
  int active;
} g_title_builder;

void d3d8_selector_probe_title_builder_enter(CPU *C) {
  uint32_t scale_bits[3];

  if (!d3d8_selector_probe_enabled())
    return;
  g_title_builder.caller = RD32(C->esp);
  g_title_builder.object = C->ecx;
  g_title_builder.translation = RD32(C->esp + 4u);
  g_title_builder.rotation = RD32(C->esp + 8u);
  scale_bits[0] = RD32(C->esp + 12u);
  scale_bits[1] = RD32(C->esp + 16u);
  scale_bits[2] = RD32(C->esp + 20u);
  memcpy(g_title_builder.scale, scale_bits, sizeof scale_bits);
  g_title_builder.active = 1;
}

void d3d8_selector_probe_title_builder_leave(void) {
  g_title_builder.active = 0;
}

static void selector_probe_transform_set_matrix(CPU *C) {
  TransformSetRecord *record =
      &g_transform_set_history[g_transform_set_count %
                               MATRIX_SET_HISTORY_CAPACITY];

  memset(record, 0, sizeof *record);
  record->destination = C->ecx + 0x20u;
  record->caller = RD32(C->esp);
  record->source = RD32(C->esp + 4u);
  record->title_builder_found = g_title_builder.active;
  if (g_title_builder.active) {
    record->title_builder_caller = g_title_builder.caller;
    record->title_builder_this = g_title_builder.object;
    record->title_builder_translation = g_title_builder.translation;
    record->title_builder_rotation = g_title_builder.rotation;
    memcpy(record->title_builder_scale, g_title_builder.scale,
           sizeof record->title_builder_scale);
  }
  g_transform_set_count++;
  x86_guest_body(C, "libIGSg.dll", 0x10003f20u);
}

static void selector_probe_matrix_copy(CPU *C) {
  MatrixCopyRecord *record =
      &g_matrix_copy_history[g_matrix_copy_count % MATRIX_SET_HISTORY_CAPACITY];

  record->destination = RD32(C->esp + 4u);
  record->caller = RD32(C->esp);
  record->source = RD32(C->esp + 8u);
  record->source_readable =
      guest_memory_is_readable(record->source, sizeof record->source_value);
  if (record->source_readable)
    memcpy(record->source_value, guest_memory_const_pointer(record->source),
           sizeof record->source_value);
  else
    memset(record->source_value, 0, sizeof record->source_value);
  g_matrix_copy_count++;
  x86_guest_body(C, "libIGMath.dll", 0x10019b90u);
}

static void selector_probe_matrix_multiply(CPU *C) {
  MatrixMultiplyRecord *record =
      &g_matrix_multiply_history[g_matrix_multiply_count %
                                 MATRIX_SET_HISTORY_CAPACITY];

  record->output = C->ecx;
  record->caller = RD32(C->esp);
  record->left = RD32(C->esp + 4u);
  record->right = RD32(C->esp + 8u);
  record->inputs_readable =
      guest_memory_is_readable(record->left, sizeof record->left_value) &&
      guest_memory_is_readable(record->right, sizeof record->right_value);
  if (record->inputs_readable) {
    memcpy(record->left_value, guest_memory_const_pointer(record->left),
           sizeof record->left_value);
    memcpy(record->right_value, guest_memory_const_pointer(record->right),
           sizeof record->right_value);
  } else {
    memset(record->left_value, 0, sizeof record->left_value);
    memset(record->right_value, 0, sizeof record->right_value);
  }
  g_matrix_multiply_count++;
  x86_guest_body(C, "libIGMath.dll", 0x10019520u);
}

static void selector_probe_attr_set_matrix(CPU *C) {
  MatrixSetRecord *record =
      &g_matrix_set_history[g_matrix_set_count % MATRIX_SET_HISTORY_CAPACITY];

  record->attr = C->ecx;
  record->caller = RD32(C->esp);
  record->source = RD32(C->esp + 4u);
  g_matrix_set_count++;
  x86_guest_body(C, "libIGAttrs.dll", 0x10003dd0u);
}

static void find_matrix_set(uint32_t matrix_guest) {
  uint32_t available = g_matrix_set_count < MATRIX_SET_HISTORY_CAPACITY
                           ? g_matrix_set_count
                           : MATRIX_SET_HISTORY_CAPACITY;
  uint32_t offset;

  g_world_matrix_set_found = 0;
  g_world_matrix_set_caller = 0;
  g_world_matrix_set_source = 0;
  for (offset = 0; offset < available; ++offset) {
    uint32_t sequence = g_matrix_set_count - 1u - offset;
    const MatrixSetRecord *record =
        &g_matrix_set_history[sequence % MATRIX_SET_HISTORY_CAPACITY];
    if (record->attr + 12u != matrix_guest)
      continue;
    g_world_matrix_set_found = 1;
    g_world_matrix_set_caller = record->caller;
    g_world_matrix_set_source = record->source;
    return;
  }
}

static void find_matrix_multiply(uint32_t matrix_guest) {
  uint32_t available = g_matrix_multiply_count < MATRIX_SET_HISTORY_CAPACITY
                           ? g_matrix_multiply_count
                           : MATRIX_SET_HISTORY_CAPACITY;
  uint32_t offset;

  g_world_matrix_multiply_found = 0;
  g_world_matrix_multiply_caller = 0;
  g_world_matrix_multiply_left = 0;
  g_world_matrix_multiply_right = 0;
  memset(g_world_matrix_multiply_left_value, 0,
         sizeof g_world_matrix_multiply_left_value);
  memset(g_world_matrix_multiply_right_value, 0,
         sizeof g_world_matrix_multiply_right_value);
  g_world_matrix_multiply_inputs_readable = 0;
  memset(g_world_matrix_multiply_chain, 0,
         sizeof g_world_matrix_multiply_chain);
  g_world_matrix_multiply_chain_count = 0;
  g_world_matrix_multiply_chain_truncated = 0;
  for (offset = 0; offset < available; ++offset) {
    uint32_t sequence = g_matrix_multiply_count - 1u - offset;
    const MatrixMultiplyRecord *record =
        &g_matrix_multiply_history[sequence % MATRIX_SET_HISTORY_CAPACITY];
    if (record->output != matrix_guest)
      continue;
    g_world_matrix_multiply_found = 1;
    g_world_matrix_multiply_caller = record->caller;
    g_world_matrix_multiply_left = record->left;
    g_world_matrix_multiply_right = record->right;
    memcpy(g_world_matrix_multiply_left_value, record->left_value,
           sizeof g_world_matrix_multiply_left_value);
    memcpy(g_world_matrix_multiply_right_value, record->right_value,
           sizeof g_world_matrix_multiply_right_value);
    g_world_matrix_multiply_inputs_readable = record->inputs_readable;
    break;
  }
  while (g_world_matrix_multiply_chain_count <
         D3D8_SELECTOR_MULTIPLY_CHAIN_MAX) {
    const MatrixMultiplyRecord *found = NULL;
    uint32_t chain_available =
        g_matrix_multiply_count < MATRIX_SET_HISTORY_CAPACITY
            ? g_matrix_multiply_count
            : MATRIX_SET_HISTORY_CAPACITY;
    for (offset = 0; offset < chain_available; ++offset) {
      uint32_t sequence = g_matrix_multiply_count - 1u - offset;
      const MatrixMultiplyRecord *record =
          &g_matrix_multiply_history[sequence % MATRIX_SET_HISTORY_CAPACITY];
      if (record->output == matrix_guest) {
        found = record;
        break;
      }
    }
    if (!found)
      return;
    {
      D3D8SelectorMultiplyStep *step =
          &g_world_matrix_multiply_chain[g_world_matrix_multiply_chain_count++];
      step->output = found->output;
      step->caller = found->caller;
      step->left = found->left;
      step->right = found->right;
      step->inputs_readable = found->inputs_readable;
      memcpy(step->left_value, found->left_value, sizeof step->left_value);
      memcpy(step->right_value, found->right_value, sizeof step->right_value);
      {
        uint32_t copy_available =
            g_matrix_copy_count < MATRIX_SET_HISTORY_CAPACITY
                ? g_matrix_copy_count
                : MATRIX_SET_HISTORY_CAPACITY;
        uint32_t copy_offset;
        for (copy_offset = 0; copy_offset < copy_available; ++copy_offset) {
          uint32_t sequence = g_matrix_copy_count - 1u - copy_offset;
          const MatrixCopyRecord *copy =
              &g_matrix_copy_history[sequence % MATRIX_SET_HISTORY_CAPACITY];
          if (copy->destination != found->left)
            continue;
          step->left_copy_found = 1;
          step->left_copy_caller = copy->caller;
          step->left_copy_source = copy->source;
          step->left_copy_source_readable = copy->source_readable;
          memcpy(step->left_copy_source_value, copy->source_value,
                 sizeof step->left_copy_source_value);
          break;
        }
      }
      {
        uint32_t set_available =
            g_transform_set_count < MATRIX_SET_HISTORY_CAPACITY
                ? g_transform_set_count
                : MATRIX_SET_HISTORY_CAPACITY;
        uint32_t set_offset;
        for (set_offset = 0; set_offset < set_available; ++set_offset) {
          uint32_t sequence = g_transform_set_count - 1u - set_offset;
          const TransformSetRecord *set =
              &g_transform_set_history[sequence % MATRIX_SET_HISTORY_CAPACITY];
          if (set->destination != found->left)
            continue;
          step->left_transform_set_found = 1;
          step->left_transform_set_caller = set->caller;
          step->left_transform_set_source = set->source;
          step->title_builder_found = set->title_builder_found;
          step->title_builder_caller = set->title_builder_caller;
          step->title_builder_this = set->title_builder_this;
          step->title_builder_translation = set->title_builder_translation;
          step->title_builder_rotation = set->title_builder_rotation;
          memcpy(step->title_builder_scale, set->title_builder_scale,
                 sizeof step->title_builder_scale);
          break;
        }
      }
      matrix_guest = found->right;
    }
  }
  g_world_matrix_multiply_chain_truncated = 1;
}

static void selector_probe_set_matrix(CPU *C) {
  uint32_t source = RD32(C->esp);
  uint32_t which = RD32(C->esp + 4u);
  uint32_t matrix_guest = RD32(C->esp + 8u);
  x86_guest_body(C, "libIGGfx.dll", 0x1003e9e0u);
  if (which == 1u) {
    g_world_matrix_source = source;
    g_world_matrix_guest = matrix_guest;
    find_matrix_set(matrix_guest);
    find_matrix_multiply(matrix_guest);
  }
}

__attribute__((constructor)) static void
selector_probe_register_matrix_source(void) {
  if (!d3d8_selector_probe_enabled())
    return;
  x86_register_override("libIGGfx.dll", 0x1003e9e0u, selector_probe_set_matrix);
  x86_register_override("libIGAttrs.dll", 0x10003dd0u,
                        selector_probe_attr_set_matrix);
  x86_register_override("libIGMath.dll", 0x10019520u,
                        selector_probe_matrix_multiply);
  x86_register_override("libIGMath.dll", 0x10019b90u,
                        selector_probe_matrix_copy);
  x86_register_override("libIGSg.dll", 0x10003f20u,
                        selector_probe_transform_set_matrix);
}

void d3d8_selector_probe_build_request(const D3D8State *state,
                                       const D3D8DrawRequest *request,
                                       unsigned long frame,
                                       D3D8SelectorProbeTicket *ticket) {
  D3D8SelectorDrawEvidence evidence;
  D3D8VertexLayout layout;
  GpuDraw geometry;
  int fixed = state->vertex_shader <= 0xf0000000u;
  int layout_valid = fixed && d3d8_fvf_layout(state->vertex_shader, &layout);

  if (ticket)
    memset(ticket, 0, sizeof *ticket);
  if (!d3d8_selector_probe_enabled())
    return;
  memset(&geometry, 0, sizeof geometry);
  geometry.vertex_stride =
      request->stride ? request->stride : (layout_valid ? layout.stride : 0u);
  geometry.pretransformed = layout_valid && layout.pretransformed;
  geometry.programmable = !fixed;
  if (layout_valid)
    d3d8_combine_transform(state, geometry.mvp);
  memset(&evidence, 0, sizeof evidence);
  evidence.request = request;
  evidence.draw = &geometry;
  evidence.state = state;
  if (request->vertex_guest_bytes && request->vertex_bytes &&
      guest_memory_is_readable(request->vertex_guest_bytes,
                               request->vertex_bytes))
    evidence.vertex_bytes =
        guest_memory_const_pointer(request->vertex_guest_bytes);
  if (request->index_guest_bytes && request->index_bytes &&
      guest_memory_is_readable(request->index_guest_bytes,
                               request->index_bytes))
    evidence.index_bytes =
        guest_memory_const_pointer(request->index_guest_bytes);
  evidence.element_count =
      d3d8_element_count(request->primitive_type, request->primitive_count);
  evidence.position_offset = layout_valid ? (uint32_t)layout.pos_offset : 0u;
  evidence.viewport_width =
      state->viewport_w > 0 ? (uint32_t)state->viewport_w : 0u;
  evidence.viewport_height =
      state->viewport_h > 0 ? (uint32_t)state->viewport_h : 0u;
  evidence.world_matrix_source = g_world_matrix_source;
  evidence.world_matrix_guest = g_world_matrix_guest;
  evidence.world_matrix_set_caller = g_world_matrix_set_caller;
  evidence.world_matrix_set_source = g_world_matrix_set_source;
  evidence.world_matrix_set_found = g_world_matrix_set_found;
  evidence.world_matrix_multiply_caller = g_world_matrix_multiply_caller;
  evidence.world_matrix_multiply_left = g_world_matrix_multiply_left;
  evidence.world_matrix_multiply_right = g_world_matrix_multiply_right;
  evidence.world_matrix_multiply_found = g_world_matrix_multiply_found;
  memcpy(evidence.world_matrix_multiply_left_value,
         g_world_matrix_multiply_left_value,
         sizeof evidence.world_matrix_multiply_left_value);
  memcpy(evidence.world_matrix_multiply_right_value,
         g_world_matrix_multiply_right_value,
         sizeof evidence.world_matrix_multiply_right_value);
  evidence.world_matrix_multiply_inputs_readable =
      g_world_matrix_multiply_inputs_readable;
  memcpy(evidence.world_matrix_multiply_chain, g_world_matrix_multiply_chain,
         sizeof evidence.world_matrix_multiply_chain);
  evidence.world_matrix_multiply_chain_count =
      g_world_matrix_multiply_chain_count;
  evidence.world_matrix_multiply_chain_truncated =
      g_world_matrix_multiply_chain_truncated;
  evidence.layout_valid = layout_valid;
  evidence.frame = frame;
  d3d8_selector_probe_request(&evidence, ticket);
}

int d3d8_build_draw(const D3D8State *state, const D3D8DrawRequest *request,
                    GpuDraw *draw) {
  D3D8SelectorProbeTicket ticket;
  int accepted;

  d3d8_selector_probe_build_request(state, request, gpu_frames_presented(),
                                    &ticket);
  accepted = d3d8_build_draw_impl(state, request, draw);
  d3d8_selector_probe_result(&ticket, accepted);
  return accepted;
}
