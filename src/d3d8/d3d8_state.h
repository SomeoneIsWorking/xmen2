/*
 * The device's render state, as D3D8 defines it.
 *
 * A D3D8 device is a big state machine that draw calls read: the engine sets
 * eighty-odd render states, eight texture stages, four transforms and a
 * material long before it asks for a triangle. So the setters do not program
 * the GPU -- they record here, and the DRAW path reads this and builds the
 * pipeline it needs. That is not an optimisation; it is the only order that
 * works, because SDL_GPU (like Vulkan under it) bakes state into a pipeline
 * object at draw time, not at set time.
 *
 * Everything here is plain host data. Nothing in this file knows what a guest
 * pointer is, which is what lets the draw path be read and tested on its own.
 */
#ifndef D3D8_STATE_H
#define D3D8_STATE_H

#include <stdint.h>

/* D3D8's render-state enum tops out below 210; 256 covers it with room for
   the driver-specific ones a title may set. An index past this is refused by
   name rather than clamped -- a clamp would silently merge two states. */
#define D3D8_MAX_RENDER_STATES  256
#define D3D8_MAX_STAGES         8
#define D3D8_MAX_STAGE_STATES   32
#define D3D8_MAX_TRANSFORMS     260      /* world matrices run to 255 + 4 */
#define D3D8_MAX_LIGHTS         16
#define D3D8_MAX_STREAMS        16

typedef struct { float m[16]; } D3D8Matrix;

typedef struct {
    uint32_t   set;                       /* has the guest ever set it */
    uint32_t   value;
} D3D8StateSlot;

typedef struct {
    uint32_t guest_ptr;                   /* the IDirect3D*8 the guest bound */
    uint32_t stride;
} D3D8StreamBinding;

typedef struct {
    D3D8StateSlot render[D3D8_MAX_RENDER_STATES];
    D3D8StateSlot stage[D3D8_MAX_STAGES][D3D8_MAX_STAGE_STATES];

    D3D8Matrix transform[D3D8_MAX_TRANSFORMS];
    uint32_t   transform_set[D3D8_MAX_TRANSFORMS];

    float      material[17];              /* D3DMATERIAL8 is 17 floats */
    uint32_t   material_set;

    float      light[D3D8_MAX_LIGHTS][26]; /* D3DLIGHT8 is 26 dwords */
    uint32_t   light_set[D3D8_MAX_LIGHTS];
    uint32_t   light_on[D3D8_MAX_LIGHTS];

    uint32_t   texture[D3D8_MAX_STAGES];  /* guest IDirect3DBaseTexture8* */

    D3D8StreamBinding stream[D3D8_MAX_STREAMS];
    uint32_t   indices;                   /* guest IDirect3DIndexBuffer8* */
    uint32_t   base_vertex_index;

    uint32_t   vertex_shader;             /* an FVF code or a shader handle */
    uint32_t   pixel_shader;

    int32_t    viewport_x, viewport_y;
    int32_t    viewport_w, viewport_h;
    float      viewport_minz, viewport_maxz;
    uint32_t   viewport_set;
} D3D8State;

void d3d8_state_reset(D3D8State *s);

/*
 * Set/get, refusing an out-of-range index by name.
 *
 * Returns 0 and reports if the index is out of range -- rather than growing
 * the array or wrapping, either of which turns a guest bug (or a wrong ABI
 * table) into corrupted state that draws wrong much later.
 */
int  d3d8_state_set_render(D3D8State *s, uint32_t which, uint32_t value);
int  d3d8_state_get_render(const D3D8State *s, uint32_t which, uint32_t *out);
int  d3d8_state_set_stage(D3D8State *s, uint32_t stage, uint32_t which,
                          uint32_t value);
int  d3d8_state_get_stage(const D3D8State *s, uint32_t stage, uint32_t which,
                          uint32_t *out);

/* What the engine has actually touched, printed at shutdown. A draw path that
   consumes three states out of ninety is a draw path that is ignoring most of
   what the engine asked for, and this is what makes that visible. */
void d3d8_state_report(const D3D8State *s);

#endif /* D3D8_STATE_H */
