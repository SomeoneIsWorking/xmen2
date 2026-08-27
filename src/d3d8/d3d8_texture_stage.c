#include "d3d8_texture_stage.h"
#include "d3d8_drawcall.h"

#include <stdio.h>
#include <string.h>

#define D3DTS_TEXTURE0 16

#define D3DTSS_COLOROP                1
#define D3DTSS_COLORARG1              2
#define D3DTSS_COLORARG2              3
#define D3DTSS_ALPHAOP                4
#define D3DTSS_ALPHAARG1              5
#define D3DTSS_ALPHAARG2              6
#define D3DTSS_TEXCOORDINDEX         11
#define D3DTSS_ADDRESSU              13
#define D3DTSS_MAGFILTER             16
#define D3DTSS_MINFILTER             17
#define D3DTSS_MIPFILTER             18
#define D3DTSS_MIPMAPLODBIAS         19
#define D3DTSS_MAXANISOTROPY         21
#define D3DTSS_TEXTURETRANSFORMFLAGS 24

#define D3DTOP_DISABLE    1
#define D3DTOP_MODULATE   4

int d3d8_texture_arg(uint32_t value, const char *what)
{
    switch (value) {
    case 0u: return GPU_TA_DIFFUSE;
    case 1u: return GPU_TA_CURRENT;
    case 2u: return GPU_TA_TEXTURE;
    case 3u: return GPU_TA_TFACTOR;
    default: {
        static int told;
        if (!told++)
            fprintf(stderr, "d3d8: %s = 0x%x is an unsupported combiner "
                            "argument (SPECULAR, TEMP, or a modifier).\n",
                    what, value);
        return -1;
    }
    }
}

int d3d8_texture_stage1_lower(const D3D8State *state, GpuTexture texture,
                              GpuDraw *draw)
{
    uint32_t op = state->stage[1][D3DTSS_COLOROP].set
                    ? state->stage[1][D3DTSS_COLOROP].value : D3DTOP_DISABLE;
    uint32_t tci, ttf, alpha_op;
    const float *matrix;
    unsigned stage;
    static const float identity[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };

    /* As at stage 0, an enabled operation that requires TEXTURE while no
       texture is bound preserves CURRENT. Alchemy leaves this stale state on
       ordinary packets; only a bound stage-1 texture makes it a second pass. */
    if (op != D3DTOP_DISABLE && texture) {
        tci = state->stage[1][D3DTSS_TEXCOORDINDEX].value;
        ttf = state->stage[1][D3DTSS_TEXTURETRANSFORMFLAGS].value;
        alpha_op = state->stage[1][D3DTSS_ALPHAOP].value;
        if (op != D3DTOP_MODULATE || alpha_op != D3DTOP_MODULATE
                || ((ttf & 0xffu) != 0u && (ttf & 0xffu) != 2u)
                || (ttf & ~0xffu) != 0u) {
            fprintf(stderr, "d3d8: stage 1 asks COLOROP %u, ALPHAOP %u, "
                            "TEXTURETRANSFORMFLAGS 0x%x; only the observed "
                            "MODULATE/MODULATE with disabled or COUNT2 "
                            "texture transform is implemented. "
                            "Draw refused.\n", op, alpha_op, ttf);
            return 0;
        }
        if ((tci & 0xffff0000u) != 0x00010000u) {
            fprintf(stderr, "d3d8: stage 1 needs a bound 2D texture and "
                            "CAMERASPACENORMAL coordinates (got texture %u, "
                            "TEXCOORDINDEX 0x%x). Draw refused.\n", texture,
                    tci);
            return 0;
        }
        draw->texture1 = texture;
        draw->texop1 = GPU_TEXOP_MODULATE;
        draw->alpha_op1 = GPU_TEXOP_MODULATE;
        draw->color_arg1_1 = d3d8_texture_arg(
            state->stage[1][D3DTSS_COLORARG1].value, "stage 1 COLORARG1");
        draw->color_arg2_1 = d3d8_texture_arg(
            state->stage[1][D3DTSS_COLORARG2].value, "stage 1 COLORARG2");
        draw->alpha_arg1_1 = d3d8_texture_arg(
            state->stage[1][D3DTSS_ALPHAARG1].value, "stage 1 ALPHAARG1");
        draw->alpha_arg2_1 = d3d8_texture_arg(
            state->stage[1][D3DTSS_ALPHAARG2].value, "stage 1 ALPHAARG2");
        if (draw->color_arg1_1 < 0 || draw->color_arg2_1 < 0
                || draw->alpha_arg1_1 < 0 || draw->alpha_arg2_1 < 0)
            return 0;
        draw->texgen1 = GPU_TEXGEN_CAMERA_NORMAL;
        draw->texture_transform1 = (int)(ttf & 0xffu);
        matrix = state->transform_set[D3DTS_TEXTURE0 + 1]
               ? state->transform[D3DTS_TEXTURE0 + 1].m : identity;
        memcpy(draw->texture_matrix1, matrix, sizeof draw->texture_matrix1);
        d3d8_worldview_transform(state, draw->worldview);
        draw->texture_clamp1 =
            state->stage[1][D3DTSS_ADDRESSU].value == 3;
        draw->texture_point1 =
            state->stage[1][D3DTSS_MAGFILTER].value == 1;
        draw->texture_min_filter1 = state->stage[1][D3DTSS_MINFILTER].set
                                  ? (int)state->stage[1][D3DTSS_MINFILTER].value
                                  : 1;
        draw->texture_max_anisotropy1 =
            state->stage[1][D3DTSS_MAXANISOTROPY].set
                ? (int)state->stage[1][D3DTSS_MAXANISOTROPY].value : 1;
        draw->texture_mip1 = state->stage[1][D3DTSS_MIPFILTER].set
                           ? (int)state->stage[1][D3DTSS_MIPFILTER].value : 0;
        if (state->stage[1][D3DTSS_MIPMAPLODBIAS].set) {
            uint32_t bits = state->stage[1][D3DTSS_MIPMAPLODBIAS].value;
            memcpy(&draw->texture_lod_bias1, &bits, sizeof bits);
        }
        if (draw->texture_mip1 < 0 || draw->texture_mip1 > 2) {
            fprintf(stderr, "d3d8: stage 1 MIPFILTER %d is not NONE, POINT, "
                            "or LINEAR; draw refused.\n", draw->texture_mip1);
            return 0;
        }
        if (draw->texture_min_filter1 < 1 || draw->texture_min_filter1 > 3) {
            fprintf(stderr, "d3d8: stage 1 MINFILTER %d is not POINT, LINEAR, "
                            "or ANISOTROPIC; draw refused.\n",
                    draw->texture_min_filter1);
            return 0;
        }
    }

    for (stage = 2; stage < D3D8_MAX_STAGES; stage++)
        if (state->stage[stage][D3DTSS_COLOROP].set
                && state->stage[stage][D3DTSS_COLOROP].value != D3DTOP_DISABLE) {
            fprintf(stderr, "d3d8: texture stage %u is enabled; this host "
                            "implements stages 0 and 1. Draw refused.\n",
                    stage);
            return 0;
        }
    return 1;
}
