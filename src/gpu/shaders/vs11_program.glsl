/*
 * A D3D8 vertex shader (VS 1.1) run on the GPU, by interpretation.
 *
 * The guest's program arrives as data rather than as shader code: the host
 * decodes it once (src/d3d8/d3d8_vs_decode.cpp) and packs it into the
 * Vs11Program block (src/d3d8/d3d8_vs_gpu.cpp, src/gpu/gpu_vs_program.h),
 * and this loop runs it per vertex. Compiling each guest program into host
 * shader code would need a shader compiler on every host at run time; one
 * build-time interpreter needs none, and a VS 1.1 program is at most 128
 * instructions of seven kinds.
 *
 * It computes what the CPU executor (src/d3d8/d3d8_vs_execute.cpp) computes,
 * register for register -- that executor is the reference the GPU selftest
 * draws this against:
 *   - one flat register file below the constants: r0-r11, v0-v16, a0, then
 *     oPos, oFog, oPts, oD0-1, oT0-7 (VS_FILE_* in d3d8_vertex_shader_internal.h);
 *   - every register starts a vertex at zero, oD0 at one;
 *   - a relative source reads c[reg + floor(a0.x + 0.5)].
 * One difference is the medium's: a relative read outside the constant file
 * makes the executor refuse the whole draw, which a vertex on the GPU cannot
 * do, so that vertex reads zero instead. A program the guest wrote correctly
 * never reaches it.
 *
 * Inputs are the guest's own vertex bytes, bound as attributes 0-15 with a
 * format per input (gpu_vs_program.c); every one is declared, because a
 * WebGPU pipeline must feed every location its shader reads.
 */
layout(location = 0) in vec4 vs11_in0;
layout(location = 1) in vec4 vs11_in1;
layout(location = 2) in vec4 vs11_in2;
layout(location = 3) in vec4 vs11_in3;
layout(location = 4) in vec4 vs11_in4;
layout(location = 5) in vec4 vs11_in5;
layout(location = 6) in vec4 vs11_in6;
layout(location = 7) in vec4 vs11_in7;
layout(location = 8) in vec4 vs11_in8;
layout(location = 9) in vec4 vs11_in9;
layout(location = 10) in vec4 vs11_in10;
layout(location = 11) in vec4 vs11_in11;
layout(location = 12) in vec4 vs11_in12;
layout(location = 13) in vec4 vs11_in13;
layout(location = 14) in vec4 vs11_in14;
layout(location = 15) in vec4 vs11_in15;

/* Must match GpuVsProgramBlock in src/gpu/gpu_vs_program.h. Each instruction
   is (op | mask << 8 | dst << 16, source 0, source 1, source 2), a source
   being reg | swizzle << 9 | negate << 17 | relative << 18. */
layout(set = 1, binding = 1) uniform Vs11Program {
    uvec4 insn[128];
    uvec4 input_type[4]; /* GPU_VS_INPUT_*, one per input */
    uvec4 info;          /* x: instruction count */
} vs11;

/* The device's whole constant file, GPU_VS_CONSTANTS in gpu_vs_program.h. */
layout(set = 1, binding = 2) uniform Vs11Constants {
    vec4 c[256];
} vs11c;

const uint VS11_FILE_INPUT = 12u;
const uint VS11_FILE_ADDR = 29u;
const uint VS11_FILE_CONST = 43u;
const uint VS11_OUT_POS = 30u;
const uint VS11_OUT_D0 = 33u;
const uint VS11_OUT_T0 = 35u;
const int VS11_CONSTANTS = 256;
/* Past this an a0 value cannot name a constant from any base. */
const float VS11_ADDRESS_LIMIT = 65536.0;

vec4 vs11_reg[43];

/* One input as the executor's load_input presents it. A FLOATn format fills
   the components it lacks with (0, 0, 1) as the executor does. */
vec4 vs11_input(vec4 raw, uint type)
{
    if (type == 0u) return vec4(0.0);           /* not in the declaration */
    if (type == 5u) return raw.zyxw;            /* D3DCOLOR: B,G,R,A bytes */
    if (type == 6u) return round(raw * 255.0);  /* UBYTE4: unnormalised */
    return raw;
}

vec4 vs11_source(uint word)
{
    uint reg = word & 0x1FFu;
    vec4 v;
    if ((word & (1u << 18)) != 0u) {
        float base = floor(vs11_reg[VS11_FILE_ADDR].x + 0.5);
        int index = -1;
        if (base > -VS11_ADDRESS_LIMIT && base < VS11_ADDRESS_LIMIT)
            index = int(reg) + int(base);
        v = index >= 0 && index < VS11_CONSTANTS ? vs11c.c[index] : vec4(0.0);
    } else if (reg >= VS11_FILE_CONST) {
        v = vs11c.c[reg - VS11_FILE_CONST];
    } else {
        v = vs11_reg[reg];
    }
    uint s = word >> 9;
    vec4 r = vec4(v[s & 3u], v[(s >> 2) & 3u], v[(s >> 4) & 3u],
                  v[(s >> 6) & 3u]);
    return (word & (1u << 17)) != 0u ? -r : r;
}

void vs11_run()
{
    /* One loop sets every starting value. Zeroing the file and then storing
       oD0 separately crashes Qualcomm's shader compiler (Adreno 722, driver
       0x8032004a: a null dereference in libllvm-qgl.so while it builds the
       pipeline), and it did so on New Game's first programmable draw. */
    for (uint i = 0u; i < VS11_FILE_CONST; i++)
        vs11_reg[i] = vec4(i == VS11_OUT_D0 ? 1.0 : 0.0);
    vs11_reg[VS11_FILE_INPUT + 0u] = vs11_input(vs11_in0, vs11.input_type[0].x);
    vs11_reg[VS11_FILE_INPUT + 1u] = vs11_input(vs11_in1, vs11.input_type[0].y);
    vs11_reg[VS11_FILE_INPUT + 2u] = vs11_input(vs11_in2, vs11.input_type[0].z);
    vs11_reg[VS11_FILE_INPUT + 3u] = vs11_input(vs11_in3, vs11.input_type[0].w);
    vs11_reg[VS11_FILE_INPUT + 4u] = vs11_input(vs11_in4, vs11.input_type[1].x);
    vs11_reg[VS11_FILE_INPUT + 5u] = vs11_input(vs11_in5, vs11.input_type[1].y);
    vs11_reg[VS11_FILE_INPUT + 6u] = vs11_input(vs11_in6, vs11.input_type[1].z);
    vs11_reg[VS11_FILE_INPUT + 7u] = vs11_input(vs11_in7, vs11.input_type[1].w);
    vs11_reg[VS11_FILE_INPUT + 8u] = vs11_input(vs11_in8, vs11.input_type[2].x);
    vs11_reg[VS11_FILE_INPUT + 9u] = vs11_input(vs11_in9, vs11.input_type[2].y);
    vs11_reg[VS11_FILE_INPUT + 10u] = vs11_input(vs11_in10, vs11.input_type[2].z);
    vs11_reg[VS11_FILE_INPUT + 11u] = vs11_input(vs11_in11, vs11.input_type[2].w);
    vs11_reg[VS11_FILE_INPUT + 12u] = vs11_input(vs11_in12, vs11.input_type[3].x);
    vs11_reg[VS11_FILE_INPUT + 13u] = vs11_input(vs11_in13, vs11.input_type[3].y);
    vs11_reg[VS11_FILE_INPUT + 14u] = vs11_input(vs11_in14, vs11.input_type[3].z);
    vs11_reg[VS11_FILE_INPUT + 15u] = vs11_input(vs11_in15, vs11.input_type[3].w);
    for (uint k = 0u; k < vs11.info.x; k++) {
        uvec4 w = vs11.insn[k];
        uint op = w.x & 0xFFu;
        uint mask = (w.x >> 8) & 0xFu;
        uint dst = w.x >> 16;
        /* All three are read before the destination is written, so a
           destination that is also a source reads its old value. */
        vec4 a = vs11_source(w.y);
        vec4 b = vs11_source(w.z);
        vec4 c = vs11_source(w.w);
        vec4 r;
        if (op == 1u) r = a;                             /* MOV */
        else if (op == 2u) r = a + b;                    /* ADD */
        else if (op == 3u) r = a - b;                    /* SUB */
        else if (op == 4u) r = a * b + c;                /* MAD */
        else if (op == 5u) r = a * b;                    /* MUL */
        else if (op == 8u) r = vec4(dot(a.xyz, b.xyz));  /* DP3 */
        else r = vec4(dot(a, b));                        /* DP4 */
        vs11_reg[dst] = mix(vs11_reg[dst], r,
                            bvec4((mask & 1u) != 0u, (mask & 2u) != 0u,
                                  (mask & 4u) != 0u, (mask & 8u) != 0u));
    }
}
