/* Opt-in evidence for draw requests bound to a selector-sized texture. */
#ifndef D3D8_SELECTOR_PROBE_H
#define D3D8_SELECTOR_PROBE_H

#include <stddef.h>
#include <stdint.h>

#include "d3d8_drawcall.h"
#include "d3d8_state.h"

struct CPU;

enum { D3D8_SELECTOR_MULTIPLY_CHAIN_MAX = 8 };

typedef struct {
    uint32_t output;
    uint32_t caller;
    uint32_t left;
    uint32_t right;
    float left_value[16];
    float right_value[16];
    int inputs_readable;
    uint32_t left_copy_caller;
    uint32_t left_copy_source;
    float left_copy_source_value[16];
    int left_copy_found;
    int left_copy_source_readable;
    uint32_t left_transform_set_caller;
    uint32_t left_transform_set_source;
    int left_transform_set_found;
    uint32_t title_builder_caller;
    uint32_t title_builder_this;
    uint32_t title_builder_translation;
    uint32_t title_builder_rotation;
    float title_builder_scale[3];
    int title_builder_found;
} D3D8SelectorMultiplyStep;

typedef struct {
    const D3D8DrawRequest *request;
    const GpuDraw *draw;
    const D3D8State *state;
    const void *vertex_bytes;
    const void *index_bytes;
    uint32_t element_count;
    uint32_t position_offset;
    uint32_t viewport_width;
    uint32_t viewport_height;
    uint32_t world_matrix_source;
    uint32_t world_matrix_guest;
    uint32_t world_matrix_set_caller;
    uint32_t world_matrix_set_source;
    int world_matrix_set_found;
    uint32_t world_matrix_multiply_caller;
    uint32_t world_matrix_multiply_left;
    uint32_t world_matrix_multiply_right;
    int world_matrix_multiply_found;
    float world_matrix_multiply_left_value[16];
    float world_matrix_multiply_right_value[16];
    int world_matrix_multiply_inputs_readable;
    D3D8SelectorMultiplyStep world_matrix_multiply_chain[
        D3D8_SELECTOR_MULTIPLY_CHAIN_MAX];
    uint32_t world_matrix_multiply_chain_count;
    int world_matrix_multiply_chain_truncated;
    int layout_valid;
    unsigned long frame;
} D3D8SelectorDrawEvidence;

typedef struct {
    int recorded;
    unsigned long frame;
    unsigned long order;
} D3D8SelectorProbeTicket;

typedef struct {
    int valid;
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;
    uint32_t requested;
    uint32_t used;
    uint32_t behind;
    uint32_t out_of_range;
    uint32_t unavailable;
} D3D8SelectorBounds;

/* Process-start opt-in. Provenance overrides are not registered when the
   probe has no output path, leaving ordinary runs on the original bodies. */
int d3d8_selector_probe_enabled(void);

/* Strict parser for the opt-in X2_SELECTOR_TEXTURE=WxH target. */
int d3d8_selector_texture_target_parse(const char *text,
                                       uint32_t *width, uint32_t *height);

/* Pure matcher for a draw bound to the requested runtime dimensions. Those
   dimensions are a discovery filter, not an asset-identity claim. */
int d3d8_selector_request_matches(
    const D3D8SelectorDrawEvidence *evidence,
    uint32_t texture_width, uint32_t texture_height);

/* Discovery target for diffuse-colour geometry with no texture bound. */
int d3d8_selector_request_is_untextured(
    const D3D8SelectorDrawEvidence *evidence);

/* Pure, uncapped bounds calculation over every element in the draw. */
int d3d8_selector_transformed_bounds(
    const D3D8SelectorDrawEvidence *evidence,
    D3D8SelectorBounds *bounds);

/* Called before and after draw lowering, so a refusal is still evidence.
   Both hooks are passive and inert unless X2_SELECTOR_PROBE names an output. */
void d3d8_selector_probe_request(const D3D8SelectorDrawEvidence *evidence,
                                 D3D8SelectorProbeTicket *ticket);
void d3d8_selector_probe_build_request(
    const D3D8State *state, const D3D8DrawRequest *request,
    unsigned long frame, D3D8SelectorProbeTicket *ticket);
void d3d8_selector_probe_result(const D3D8SelectorProbeTicket *ticket,
                                int accepted);

/* Optional title-transform provenance bracket. The production owner of the
   transform builder calls this around its retained recompiled body. */
void d3d8_selector_probe_title_builder_enter(struct CPU *cpu);
void d3d8_selector_probe_title_builder_leave(void);

#endif /* D3D8_SELECTOR_PROBE_H */
