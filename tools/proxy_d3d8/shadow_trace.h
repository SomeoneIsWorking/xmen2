#ifndef X2_PROXY_SHADOW_TRACE_H
#define X2_PROXY_SHADOW_TRACE_H

#include <stdint.h>
#include <stdio.h>

#define SHADOW_TRACE_STAGES 8
#define SHADOW_TRACE_STAGE_STATES 32
#define SHADOW_TRACE_TRANSFORMS 512
#define SHADOW_TRACE_RENDER_STATES 256

typedef enum {
  SHADOW_RESOURCE_CREATE_TEXTURE,
  SHADOW_RESOURCE_GET_SURFACE_LEVEL,
  SHADOW_RESOURCE_CREATE_RENDER_TARGET,
  SHADOW_RESOURCE_CREATE_DEPTH_STENCIL,
  SHADOW_RESOURCE_COPY_RECTS,
  SHADOW_RESOURCE_UPDATE_TEXTURE,
  SHADOW_RESOURCE_DEFAULT_RENDER_TARGET,
  SHADOW_RESOURCE_DEFAULT_DEPTH_STENCIL,
  SHADOW_RESOURCE_COUNT
} ShadowResourceKind;

typedef struct {
  int set;
  float m[16];
} ShadowTransform;

typedef struct {
  FILE *out;
  int enabled;
  int expected_detailed;
  unsigned max_events;
  unsigned frame;
  int capturing;
  int capture_detailed;
  int arm_down;
  unsigned events;
  unsigned dropped_events;
  unsigned resource_events;
  unsigned dropped_resource_events;
  unsigned texture_hook_failures;
  unsigned draws;
  unsigned indexed_draws;
  unsigned frame_resources[SHADOW_RESOURCE_COUNT];
  unsigned total_resources[SHADOW_RESOURCE_COUNT];

  uintptr_t render_target;
  uintptr_t depth_stencil;
  int render_target_known;
  int depth_stencil_known;
  int render_target_is_default;
  int depth_stencil_is_default;
  uintptr_t texture[SHADOW_TRACE_STAGES];
  uint32_t pixel_shader;
  uint32_t render_state[SHADOW_TRACE_RENDER_STATES];
  unsigned char render_state_set[SHADOW_TRACE_RENDER_STATES];
  uint32_t stage_state[SHADOW_TRACE_STAGES][SHADOW_TRACE_STAGE_STATES];
  unsigned char stage_state_set[SHADOW_TRACE_STAGES][SHADOW_TRACE_STAGE_STATES];
  ShadowTransform transform[SHADOW_TRACE_TRANSFORMS];
} ShadowTrace;

void shadow_trace_init(ShadowTrace *trace, FILE *out, int expected_detailed,
                       unsigned max_events);
int shadow_trace_parse_binary_control(const char *value, int *parsed);
int shadow_trace_setting_anchor_matches(const unsigned char *instruction,
                                        size_t instruction_size);
void shadow_trace_control(ShadowTrace *trace, int original_detailed,
                          int forced_detailed, int observed_detailed,
                          unsigned forced_reads);
void shadow_trace_present(ShadowTrace *trace, int detailed_shadow,
                          int arm_key_down);
void shadow_trace_close(ShadowTrace *trace);

void shadow_trace_resource(ShadowTrace *trace, ShadowResourceKind kind,
                           uintptr_t object, uintptr_t related, uint32_t a,
                           uint32_t b, uint32_t c, uint32_t d, uint32_t e,
                           uint32_t f, long result);
void shadow_trace_texture_hook_failure(ShadowTrace *trace);
void shadow_trace_default_targets(ShadowTrace *trace, uintptr_t target,
                                  long target_result, uintptr_t depth_stencil,
                                  long depth_result);
void shadow_trace_set_render_target(ShadowTrace *trace, uintptr_t target,
                                    uintptr_t depth_stencil);
void shadow_trace_set_transform(ShadowTrace *trace, uint32_t which,
                                const float matrix[16]);
void shadow_trace_set_render_state(ShadowTrace *trace, uint32_t which,
                                   uint32_t value);
void shadow_trace_set_texture(ShadowTrace *trace, uint32_t stage,
                              uintptr_t texture);
void shadow_trace_set_stage_state(ShadowTrace *trace, uint32_t stage,
                                  uint32_t which, uint32_t value);
void shadow_trace_set_pixel_shader(ShadowTrace *trace, uint32_t handle);
void shadow_trace_draw(ShadowTrace *trace, int indexed, uint32_t primitive,
                       uint32_t primitive_count);
void shadow_trace_clear(ShadowTrace *trace, uint32_t rect_count,
                        const int32_t *rects, uint32_t flags, uint32_t color,
                        float depth, uint32_t stencil, long result);

#endif
