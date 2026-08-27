/* Opt-in evidence for draw requests bound to a selector-sized texture. */
#ifndef D3D8_SELECTOR_PROBE_H
#define D3D8_SELECTOR_PROBE_H

#include <stddef.h>
#include <stdint.h>

#include "d3d8_drawcall.h"
#include "d3d8_state.h"

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

/* Strict parser for the opt-in X2_SELECTOR_TEXTURE=WxH target. */
int d3d8_selector_texture_target_parse(const char *text,
                                       uint32_t *width, uint32_t *height);

/* Pure matcher for a draw bound to the requested runtime dimensions. Those
   dimensions are a discovery filter, not an asset-identity claim. */
int d3d8_selector_request_matches(
    const D3D8SelectorDrawEvidence *evidence,
    uint32_t texture_width, uint32_t texture_height);

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

#endif /* D3D8_SELECTOR_PROBE_H */
