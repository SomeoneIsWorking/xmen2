/*
 * D3D8 vertex-shader handle ownership.
 *
 * D3D8 overloads SetVertexShader: values through 0xf0000000 are FVF codes,
 * while created shaders are opaque handles above that boundary. The guest
 * keeps those handles, so every slot carries a generation; deleting and then
 * reusing a slot cannot make an old handle silently name a different shader.
 */
#include "d3d8_vertex_shader.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

#define VS_MAX 64
#define VS_HANDLE_BASE 0xf0000001u
#define VS_DECL_MAX_DWORDS 256
#define VS_CODE_MAX_DWORDS 4096

struct D3D8VertexShader {
    int used;
    uint16_t generation;
    uint32_t usage;
    uint32_t declaration[VS_DECL_MAX_DWORDS];
    uint32_t function[VS_CODE_MAX_DWORDS];
    uint16_t declaration_dwords;
    uint16_t function_dwords;
};

static D3D8VertexShader g_shader[VS_MAX];
static uint16_t g_generation = 1;
static unsigned long g_created, g_deleted, g_refused;
static unsigned long g_executions, g_vertices;

static uint32_t make_handle(unsigned slot, uint16_t generation)
{
    return VS_HANDLE_BASE + ((uint32_t)generation << 8) + slot;
}

static int scan_declaration(const uint32_t *p, uint16_t *count)
{
    unsigned i;
    if (!p) return 0;
    for (i = 0; i < VS_DECL_MAX_DWORDS; ++i) {
        if (p[i] == 0xffffffffu) { *count = (uint16_t)(i + 1); return 1; }
    }
    fprintf(stderr, "d3d8: CreateVertexShader scanned %u declaration token(s) "
                    "but found no D3DVSD_END; refusing an unterminated stream.\n",
            VS_DECL_MAX_DWORDS);
    return 0;
}

static int scan_function(const uint32_t *p, uint16_t *count)
{
    unsigned i;
    if (!p) { *count = 0; return 1; } /* declaration-only shaders are legal */
    if (p[0] != 0xfffe0101u) {
        fprintf(stderr, "d3d8: CreateVertexShader function begins 0x%08x, not "
                        "the VS 1.1 version token 0xfffe0101.\n", p[0]);
        return 0;
    }
    for (i = 1; i < VS_CODE_MAX_DWORDS; ++i) {
        if ((p[i] & 0xffffu) == 0xffffu) {
            *count = (uint16_t)(i + 1);
            return 1;
        }
    }
    fprintf(stderr, "d3d8: CreateVertexShader scanned %u bytecode dword(s) but "
                    "found no D3DSIO_END; refusing an unterminated program.\n",
            VS_CODE_MAX_DWORDS);
    return 0;
}

uint32_t d3d8_vs_create(const uint32_t *declaration, const uint32_t *function,
                        uint32_t usage)
{
    uint16_t nd, nf;
    unsigned i;
    uint32_t h;
    if (!scan_declaration(declaration, &nd) || !scan_function(function, &nf)) {
        g_refused++;
        return 0;
    }
    for (i = 0; i < VS_MAX && g_shader[i].used; ++i) {}
    if (i == VS_MAX) {
        fprintf(stderr, "d3d8: CreateVertexShader found all %u shader slots "
                        "live; refusing instead of losing an existing shader.\n",
                VS_MAX);
        g_refused++;
        return 0;
    }
    memset(&g_shader[i], 0, sizeof g_shader[i]);
    g_shader[i].used = 1;
    g_shader[i].generation = g_generation++;
    if (!g_generation) g_generation = 1;
    g_shader[i].usage = usage;
    g_shader[i].declaration_dwords = nd;
    g_shader[i].function_dwords = nf;
    memcpy(g_shader[i].declaration, declaration, nd * sizeof *declaration);
    if (nf) memcpy(g_shader[i].function, function, nf * sizeof *function);
    h = make_handle(i, g_shader[i].generation);
    g_created++;
    printf("d3d8: CreateVertexShader -> 0x%08x (%u declaration token(s), "
           "%u bytecode dword(s), usage 0x%x)\n", h, nd, nf, usage);
    return h;
}

D3D8VertexShader *d3d8_vs_get(uint32_t handle, const char *operation)
{
    uint32_t raw;
    unsigned slot;
    uint16_t generation;
    if (handle <= 0xf0000000u) {
        fprintf(stderr, "d3d8: %s received 0x%08x, an FVF value rather than a "
                        "created vertex-shader handle.\n", operation, handle);
        g_refused++;
        return NULL;
    }
    raw = handle - VS_HANDLE_BASE;
    slot = raw & 0xffu;
    generation = (uint16_t)(raw >> 8);
    if (slot >= VS_MAX || !g_shader[slot].used
            || g_shader[slot].generation != generation) {
        fprintf(stderr, "d3d8: %s received 0x%08x, which names no live vertex "
                        "shader (slot %u, generation %u).\n",
                operation, handle, slot, generation);
        g_refused++;
        return NULL;
    }
    return &g_shader[slot];
}

int d3d8_vs_delete(uint32_t handle)
{
    D3D8VertexShader *s = d3d8_vs_get(handle, "DeleteVertexShader");
    if (!s) return 0;
    s->used = 0;
    g_deleted++;
    return 1;
}

void d3d8_vs_reset(void)
{
    unsigned i;
    for (i = 0; i < VS_MAX; ++i) g_shader[i].used = 0;
}

const uint32_t *d3d8_vs_declaration(const D3D8VertexShader *s, size_t *bytes)
{
    *bytes = (size_t)s->declaration_dwords * sizeof(uint32_t);
    return s->declaration;
}

const uint32_t *d3d8_vs_function(const D3D8VertexShader *s, size_t *bytes)
{
    *bytes = (size_t)s->function_dwords * sizeof(uint32_t);
    return s->function_dwords ? s->function : NULL;
}

typedef struct { float x[4]; } Vec;

static unsigned reg_type(uint32_t t)
{
    return ((t >> 28) & 7u) | ((t >> 8) & 0x18u);
}

static unsigned data_size(unsigned type)
{
    static const unsigned sizes[8] = {4, 8, 12, 16, 4, 4, 4, 8};
    return type < 8 ? sizes[type] : 0;
}

typedef struct { int present; uint16_t offset; uint8_t type; } Input;

static int decode_inputs(const D3D8VertexShader *s, Input input[17])
{
    unsigned stream = 0, offset[16] = {0}, i;
    memset(input, 0, 17 * sizeof *input);
    for (i = 0; i < s->declaration_dwords; ++i) {
        uint32_t t = s->declaration[i], kind = (t >> 29) & 7u;
        if (t == 0xffffffffu) return 1;
        if (kind == 1) {
            stream = t & 0xfu;
            if (stream != 0) {
                fprintf(stderr, "d3d8: vertex declaration selects stream %u; "
                                "the VS executor currently has only stream 0.\n",
                        stream);
                return 0;
            }
        } else if (kind == 2) {
            if (t & 0x10000000u) {
                offset[stream] += ((t >> 16) & 0xfu) * 4u;
            } else {
                unsigned reg = t & 0x1fu, type = (t >> 16) & 0xfu;
                unsigned n = data_size(type);
                if (reg >= 17 || !n) {
                    fprintf(stderr, "d3d8: vertex declaration REG %u type %u "
                                    "cannot be represented.\n", reg, type);
                    return 0;
                }
                input[reg].present = 1;
                input[reg].offset = (uint16_t)offset[stream];
                input[reg].type = (uint8_t)type;
                offset[stream] += n;
            }
        } else if (kind != 0) {
            fprintf(stderr, "d3d8: vertex declaration token 0x%08x has "
                            "unsupported token type %u.\n", t, kind);
            return 0;
        }
    }
    fprintf(stderr, "d3d8: scanned %u declaration token(s), but no END was "
                    "reachable in the copied stream.\n", s->declaration_dwords);
    return 0;
}

static Vec load_input(const uint8_t *p, unsigned type)
{
    Vec v = {{0, 0, 0, 1}};
    unsigned i;
    if (type <= 3) {
        unsigned n = type + 1;
        memcpy(v.x, p, n * sizeof(float));
    } else if (type == 4 || type == 5) {
        /* D3DCOLOR is ARGB numerically / BGRA in little-endian memory and is
           presented to a shader as RGBA. UBYTE4 preserves byte order. */
        if (type == 4) {
            v.x[0] = p[2] / 255.0f; v.x[1] = p[1] / 255.0f;
            v.x[2] = p[0] / 255.0f; v.x[3] = p[3] / 255.0f;
        } else for (i = 0; i < 4; ++i) v.x[i] = (float)p[i];
    } else {
        const int16_t *q = (const int16_t *)p;
        unsigned n = type == 6 ? 2 : 4;
        for (i = 0; i < n; ++i) v.x[i] = (float)q[i];
    }
    return v;
}

static Vec source(uint32_t t, Vec temp[12], Vec in[17], Vec c[96],
                  Vec *addr, Vec out[12], int *ok)
{
    unsigned type = reg_type(t), n = t & 0x7ffu, i;
    Vec raw = {{0, 0, 0, 0}}, v;
    if (type == 0 && n < 12) raw = temp[n];
    else if (type == 1 && n < 17) raw = in[n];
    else if (type == 2) {
        int idx = (int)n;
        if (t & 0x00002000u) idx += (int)floorf(addr->x[0] + 0.5f);
        if (idx < 0 || idx >= 96) {
            fprintf(stderr, "d3d8: VS 1.1 indexed constant c[%d] is outside "
                            "the 96-register file.\n", idx);
            *ok = 0; return raw;
        }
        raw = c[idx];
    } else if (type == 3 && n == 0) raw = *addr;
    else if (type == 4 && n < 3) raw = out[n];
    else if (type == 5 && n < 2) raw = out[3 + n];
    else if (type == 6 && n < 8) raw = out[5 + n];
    else {
        fprintf(stderr, "d3d8: VS 1.1 source register type %u number %u is "
                        "unsupported.\n", type, n);
        *ok = 0; return raw;
    }
    for (i = 0; i < 4; ++i) v.x[i] = raw.x[(t >> (16 + i * 2)) & 3u];
    if (((t >> 24) & 0xfu) == 1) for (i = 0; i < 4; ++i) v.x[i] = -v.x[i];
    else if ((t >> 24) & 0xfu) {
        fprintf(stderr, "d3d8: VS 1.1 source modifier %u is unsupported.\n",
                (t >> 24) & 0xfu);
        *ok = 0;
    }
    return v;
}

static Vec *destination(uint32_t t, Vec temp[12], Vec *addr, Vec out[12],
                        int *ok)
{
    unsigned type = reg_type(t), n = t & 0x7ffu;
    if (type == 0 && n < 12) return &temp[n];
    if (type == 3 && n == 0) return addr;
    if (type == 4 && n < 3) return &out[n];
    if (type == 5 && n < 2) return &out[3 + n];
    if (type == 6 && n < 8) return &out[5 + n];
    fprintf(stderr, "d3d8: VS 1.1 destination register type %u number %u is "
                    "unsupported.\n", type, n);
    *ok = 0; return &temp[0];
}

static void write_result(uint32_t token, Vec *dst, Vec value)
{
    unsigned mask = (token >> 16) & 0xfu, i;
    if (!mask) mask = 0xf;
    for (i = 0; i < 4; ++i) if (mask & (1u << i)) dst->x[i] = value.x[i];
}

static int execute_one(const D3D8VertexShader *s, Vec input[17], Vec constants[96],
                       D3D8VSOutput *result)
{
    Vec r[12] = {{{0}}}, o[13] = {{{0}}}, a = {{0,0,0,0}};
    unsigned pc = 1;
    int ok = 1;
    o[3].x[0] = o[3].x[1] = o[3].x[2] = o[3].x[3] = 1.0f;
    while (pc < s->function_dwords) {
        uint32_t op = s->function[pc++] & 0xffffu, d;
        Vec x, y, z, v = {{0,0,0,0}}; unsigned i;
        if (op == 0xffffu) break;
        if (pc >= s->function_dwords) return 0;
        d = s->function[pc++];
        switch (op) {
        case 1: /* MOV */
            x = source(s->function[pc++], r, input, constants, &a, o, &ok); v=x; break;
        case 2: /* ADD */
        case 3: /* SUB */
        case 5: /* MUL */
            x=source(s->function[pc++],r,input,constants,&a,o,&ok);
            y=source(s->function[pc++],r,input,constants,&a,o,&ok);
            for(i=0;i<4;i++) v.x[i]=op==2?x.x[i]+y.x[i]:op==3?x.x[i]-y.x[i]:x.x[i]*y.x[i];
            break;
        case 4: /* MAD */
            x=source(s->function[pc++],r,input,constants,&a,o,&ok);
            y=source(s->function[pc++],r,input,constants,&a,o,&ok);
            z=source(s->function[pc++],r,input,constants,&a,o,&ok);
            for(i=0;i<4;i++) v.x[i]=x.x[i]*y.x[i]+z.x[i]; break;
        case 8: /* DP3 */
        case 9: /* DP4 */ {
            float dot=0; unsigned n=op==8?3:4;
            x=source(s->function[pc++],r,input,constants,&a,o,&ok);
            y=source(s->function[pc++],r,input,constants,&a,o,&ok);
            for(i=0;i<n;i++) dot += x.x[i]*y.x[i];
            for(i=0;i<4;i++) v.x[i]=dot; break;
        }
        default:
            fprintf(stderr, "d3d8: VS 1.1 opcode %u at dword %u is not "
                            "implemented; the draw is refused.\n", op, pc-2);
            return 0;
        }
        if (!ok) return 0;
        write_result(d, destination(d,r,&a,o,&ok), v);
        if (!ok) return 0;
    }
    memcpy(result->position, o[0].x, sizeof result->position); /* oPos */
    memcpy(result->diffuse, o[3].x, sizeof result->diffuse);  /* oD0 */
    result->texcoord[0] = o[5].x[0]; result->texcoord[1] = o[5].x[1]; /* oT0 */
    return 1;
}

int d3d8_vs_execute(uint32_t handle, const float constants[96][4],
                    const void *vertices, uint32_t vertex_bytes,
                    uint32_t stride, uint32_t first, uint32_t count,
                    D3D8VSOutput *output)
{
    D3D8VertexShader *s = d3d8_vs_get(handle, "draw");
    Input decl[17]; Vec c[96]; unsigned v, i;
    const uint8_t *base = vertices;
    if (!s || !vertices || !stride || !output || !decode_inputs(s, decl)) return 0;
    if (first > UINT32_MAX - count || (uint64_t)(first + count) * stride > vertex_bytes) {
        fprintf(stderr, "d3d8: vertex shader draw asks for vertices %u..%u at "
                        "stride %u from a %u-byte source.\n",
                first, first + count, stride, vertex_bytes);
        return 0;
    }
    memcpy(c, constants, sizeof c);
    for (v = 0; v < count; ++v) {
        Vec in[17] = {{{0}}};
        const uint8_t *p = base + (first + v) * stride;
        for (i = 0; i < 17; ++i) if (decl[i].present) {
            unsigned n = data_size(decl[i].type);
            if ((unsigned)decl[i].offset + n > stride) {
                fprintf(stderr, "d3d8: declaration input v%u ends at byte %u, "
                                "past stride %u.\n", i, decl[i].offset+n, stride);
                return 0;
            }
            in[i] = load_input(p + decl[i].offset, decl[i].type);
        }
        if (!execute_one(s, in, c, &output[v])) return 0;
    }
    g_executions++;
    g_vertices += count;
    return 1;
}

void d3d8_vs_report(void)
{
    unsigned i, live = 0;
    for (i = 0; i < VS_MAX; ++i) if (g_shader[i].used) live++;
    printf("  d3d8: %lu vertex shader(s) created, %lu deleted, %u still live; "
           "%lu shader-lifecycle call(s) refused\n"
           "        VS 1.1 executor ran %lu draw(s), %lu vertex invocation(s)\n",
           g_created, g_deleted, live, g_refused, g_executions, g_vertices);
    if (g_created && !g_executions)
        printf("        NO created shader was executed -- this run proves the "
               "lifecycle only, not programmable drawing.\n");
    /* What the engine actually BOUND. "0 created" alone cannot tell an engine
       that wanted no shader from one that asked and was turned away. */
    d3d8_vertex_shader_binding_report();
}

int d3d8_vs_selftest(void)
{
    /* v0=float4 position, v1=float1 matrix selector, v2=float4 colour. */
    static const uint32_t decl[] = {
        0x20000000, 0x40030000, 0x40000001, 0x40030002, 0xffffffff
    };
    static const uint32_t code[] = {
        0xfffe0101,
        0x00000001, 0xb0010000, 0x90000001, /* mov a0.x, v1.x */
        0x00000009, 0xc0010000, 0xa0e42001, 0x90e40000,
        0x00000009, 0xc0020000, 0xa0e42002, 0x90e40000,
        0x00000009, 0xc0040000, 0xa0e42003, 0x90e40000,
        0x00000009, 0xc0080000, 0xa0e42004, 0x90e40000,
        0x00000001, 0x500f0000, 0x90e40002, /* mov oD0, v2 */
        0x0000ffff
    };
    static const uint32_t bad_code[] = {
        0xfffe0101, 0x00001234, 0x800f0000, 0x0000ffff
    };
    struct { float p[4], selector, colour[4]; } vertex = {
        {1, 2, 3, 1}, 1, {0.25f, 0.5f, 0.75f, 1.0f}
    };
    float c[96][4] = {{0}};
    D3D8VSOutput out;
    uint32_t h, bad;
    int fails = 0;

    /* a0.x=1, so c[a0+1..4] = c2..c5. Four independent rows make
       relative addressing, masks, and DP4 all observable. */
    c[2][0]=2; c[3][1]=3; c[4][2]=4; c[5][3]=5;
    h = d3d8_vs_create(decl, code, 0);
    if (!h || !d3d8_vs_execute(h, c, &vertex, sizeof vertex, sizeof vertex,
                               0, 1, &out)
            || fabsf(out.position[0]-2) > 0.0001f
            || fabsf(out.position[1]-6) > 0.0001f
            || fabsf(out.position[2]-12) > 0.0001f
            || fabsf(out.position[3]-5) > 0.0001f
            || memcmp(out.diffuse, vertex.colour, sizeof vertex.colour)) {
        printf("d3d8 VS selftest: FAILED -- relative DP4 output was "
               "[%g %g %g %g], expected [2 6 12 5].\n",
               out.position[0],out.position[1],out.position[2],out.position[3]);
        fails++;
    }
    if (h) d3d8_vs_delete(h);

    bad = d3d8_vs_create(decl, bad_code, 0);
    if (!bad || d3d8_vs_execute(bad, c, &vertex, sizeof vertex, sizeof vertex,
                                0, 1, &out)) {
        printf("d3d8 VS selftest: FAILED -- unsupported opcode 0x1234 was not "
               "refused by the shipping executor.\n");
        fails++;
    }
    if (bad) d3d8_vs_delete(bad);
    printf("d3d8 VS selftest: %s -- positive relative-addressed program and "
           "negative unsupported-opcode program both exercised\n",
           fails ? "FAILED" : "PASSED");
    return fails;
}
