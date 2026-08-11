/*
 * The SSE model in x86rt.h, against the CPU's OWN SSE instructions.
 *
 * The model deliberately calls host intrinsics for the arithmetic, so a naive
 * reading of this test is "it compares the host to itself". It does not. What
 * is being checked is everything AROUND the arithmetic, which is where a
 * translation of SSE actually goes wrong:
 *
 *   - which lanes an instruction writes and which it PRESERVES (the scalar
 *     forms keep lanes 1..3 of the destination; MOVSS between registers keeps
 *     them and MOVSS from memory zeroes them -- one mnemonic, two behaviours);
 *   - which operand supplies which half (SHUFPS takes its low pair from the
 *     DESTINATION, MOVHLPS moves source-high to dest-low, ANDNPS is
 *     ~dest & src and not dest & ~src);
 *   - that the intrinsic chosen for a mnemonic is the one that mnemonic means.
 *
 * That last point is why the corpus is full of NaNs, signed zeros, infinities
 * and denormals. MINPS is exactly C's `dst < src ? dst : src` -- INCLUDING the
 * NaN case, since a comparison against a NaN is false and the second operand
 * wins -- but it is NOT fminf(), which returns whichever operand is not a NaN
 * and prefers -0.0 over +0.0. That is the mistake worth guarding against, and
 * the discriminator at the end proves this corpus can tell the two apart
 * rather than asserting it.
 *
 * The host is x86-64, so every instruction below is executed for real.
 */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "x86rt.h"

/* The runtime's globals, so this links without the recompiled world. */
uint32_t g_imgbase = 0x10000000U;
uint32_t g_image_lo, g_image_hi;
__thread uint32_t g_fsbase, g_gsbase;
int x86_allow_fallback;

static int fails, checks;

typedef struct { uint64_t q[2]; } V128;

static V128 vf(float a, float b, float c, float d)
{
    V128 v; float f[4]; f[0] = a; f[1] = b; f[2] = c; f[3] = d;
    memcpy(&v, f, 16); return v;
}
static V128 vb(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    V128 v; uint32_t u[4]; u[0] = a; u[1] = b; u[2] = c; u[3] = d;
    memcpy(&v, u, 16); return v;
}

static void report(const char *what, V128 host, V128 model, V128 a, V128 b)
{
    checks++;
    if (host.q[0] == model.q[0] && host.q[1] == model.q[1])
        return;
    printf("  FAIL %-10s a=%016llx%016llx b=%016llx%016llx\n"
           "        host  %016llx%016llx\n"
           "        model %016llx%016llx\n", what,
           (unsigned long long)a.q[1], (unsigned long long)a.q[0],
           (unsigned long long)b.q[1], (unsigned long long)b.q[0],
           (unsigned long long)host.q[1], (unsigned long long)host.q[0],
           (unsigned long long)model.q[1], (unsigned long long)model.q[0]);
    fails++;
}

/* ---- the silicon ------------------------------------------------------- */

#define HOST_BIN(fn, insn)                                                    \
    static V128 fn(V128 a, V128 b)                                            \
    {                                                                         \
        V128 out;                                                             \
        __asm__ volatile("movups %1, %%xmm0\n\t"                              \
                         "movups %2, %%xmm1\n\t"                              \
                         insn " %%xmm1, %%xmm0\n\t"                           \
                         "movups %%xmm0, %0"                                  \
                         : "=m"(out) : "m"(a), "m"(b) : "xmm0", "xmm1");      \
        return out;                                                           \
    }

HOST_BIN(h_addps, "addps")   HOST_BIN(h_subps, "subps")
HOST_BIN(h_mulps, "mulps")   HOST_BIN(h_divps, "divps")
HOST_BIN(h_minps, "minps")   HOST_BIN(h_maxps, "maxps")
HOST_BIN(h_andps, "andps")   HOST_BIN(h_andnps, "andnps")
HOST_BIN(h_orps,  "orps")    HOST_BIN(h_xorps, "xorps")
HOST_BIN(h_addss, "addss")   HOST_BIN(h_subss, "subss")
HOST_BIN(h_mulss, "mulss")   HOST_BIN(h_divss, "divss")
HOST_BIN(h_minss, "minss")   HOST_BIN(h_maxss, "maxss")
HOST_BIN(h_unpcklps, "unpcklps") HOST_BIN(h_unpckhps, "unpckhps")
HOST_BIN(h_movhlps, "movhlps")   HOST_BIN(h_movlhps, "movlhps")
HOST_BIN(h_sqrtps, "sqrtps") HOST_BIN(h_rcpps, "rcpps")
HOST_BIN(h_rsqrtps, "rsqrtps")
HOST_BIN(h_sqrtss, "sqrtss") HOST_BIN(h_rcpss, "rcpss")
HOST_BIN(h_rsqrtss, "rsqrtss")
HOST_BIN(h_movss, "movss")

static uint32_t h_movmskps(V128 a)
{
    uint32_t r;
    __asm__ volatile("movups %1, %%xmm0\n\t"
                     "movmskps %%xmm0, %0"
                     : "=r"(r) : "m"(a) : "xmm0");
    return r;
}

/* COMISS writes ZF/PF/CF; the whole EFLAGS word comes back so nothing is
   assumed about the bits it leaves alone. */
static uint32_t h_comiss(V128 a, V128 b)
{
    uint64_t f;
    __asm__ volatile("movups %1, %%xmm0\n\t"
                     "movups %2, %%xmm1\n\t"
                     "comiss %%xmm1, %%xmm0\n\t"
                     "pushfq\n\t"
                     "pop %0"
                     : "=r"(f) : "m"(a), "m"(b) : "xmm0", "xmm1", "cc");
    return (uint32_t)f;
}

#define CMP1(P)                                                               \
    case (P):                                                                 \
        __asm__ volatile("movups %1, %%xmm0\n\t"                              \
                         "movups %2, %%xmm1\n\t"                              \
                         "cmpps %3, %%xmm1, %%xmm0\n\t"                       \
                         "movups %%xmm0, %0"                                  \
                         : "=m"(out) : "m"(a), "m"(b), "i"((int)(P))          \
                         : "xmm0", "xmm1");                                   \
        break;
static V128 h_cmpps(V128 a, V128 b, int pred)
{
    V128 out;
    switch (pred) {
    CMP1(0) CMP1(1) CMP1(2) CMP1(3) CMP1(4) CMP1(5) CMP1(6) CMP1(7)
    default: out = a; break;
    }
    return out;
}

#define CMPS1(P)                                                              \
    case (P):                                                                 \
        __asm__ volatile("movups %1, %%xmm0\n\t"                              \
                         "movups %2, %%xmm1\n\t"                              \
                         "cmpss %3, %%xmm1, %%xmm0\n\t"                       \
                         "movups %%xmm0, %0"                                  \
                         : "=m"(out) : "m"(a), "m"(b), "i"((int)(P))          \
                         : "xmm0", "xmm1");                                   \
        break;
static V128 h_cmpss(V128 a, V128 b, int pred)
{
    V128 out;
    switch (pred) {
    CMPS1(0) CMPS1(1) CMPS1(2) CMPS1(3) CMPS1(4) CMPS1(5) CMPS1(6) CMPS1(7)
    default: out = a; break;
    }
    return out;
}

/* All 256 immediates, because SHUFPS is where "which operand supplies which
   half" is decided and a spread would leave most of the table unvisited. */
#define SH1(I)                                                                \
    case (I):                                                                 \
        __asm__ volatile("movups %1, %%xmm0\n\t"                              \
                         "movups %2, %%xmm1\n\t"                              \
                         "shufps %3, %%xmm1, %%xmm0\n\t"                      \
                         "movups %%xmm0, %0"                                  \
                         : "=m"(out) : "m"(a), "m"(b), "i"((int)(I))          \
                         : "xmm0", "xmm1");                                   \
        break;
#define SH4(B)   SH1((B)+0)  SH1((B)+1)  SH1((B)+2)  SH1((B)+3)
#define SH16(B)  SH4((B)+0)  SH4((B)+4)  SH4((B)+8)  SH4((B)+12)
#define SH64(B)  SH16((B)+0) SH16((B)+16) SH16((B)+32) SH16((B)+48)
static V128 h_shufps(V128 a, V128 b, int imm)
{
    V128 out;
    switch (imm) {
    SH64(0) SH64(64) SH64(128) SH64(192)
    default: out = a; break;
    }
    return out;
}

/* ---- the corpus -------------------------------------------------------- */

#define QNAN 0x7FC00000u
#define SNAN 0x7F800001u
#define PINF 0x7F800000u
#define NINF 0xFF800000u
#define DEN  0x00000001u      /* smallest positive denormal */

int main(void)
{
    V128 corpus[12];
    int n = 0, i, j, p, imm;

    corpus[n++] = vf(1.0f, 2.0f, 3.0f, 4.0f);
    corpus[n++] = vf(-1.5f, 0.25f, -0.0f, 0.0f);
    corpus[n++] = vf(0.0f, -0.0f, 1.0f, -1.0f);
    corpus[n++] = vb(QNAN, 0x3F800000u, PINF, NINF);
    corpus[n++] = vb(SNAN, DEN, 0x80000001u, 0x00800000u);
    corpus[n++] = vf(1e30f, 1e-30f, 3.4028235e38f, 1.1754944e-38f);
    corpus[n++] = vb(0xFFFFFFFFu, 0x00000000u, 0xAAAAAAAAu, 0x55555555u);
    corpus[n++] = vf(0.0f, 0.0f, 0.0f, 0.0f);
    corpus[n++] = vf(7.5f, -7.5f, 0.5f, 256.0f);
    corpus[n++] = vb(PINF, PINF, QNAN, DEN);

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            V128 a = corpus[i], b = corpus[j], m;

#define CHK(name, hostfn, modelfn)                                            \
            m = a; modelfn(m.q, b.q);                                         \
            report(name, hostfn(a, b), m, a, b);

            CHK("ADDPS", h_addps, sse_addps)
            CHK("SUBPS", h_subps, sse_subps)
            CHK("MULPS", h_mulps, sse_mulps)
            CHK("DIVPS", h_divps, sse_divps)
            CHK("MINPS", h_minps, sse_minps)
            CHK("MAXPS", h_maxps, sse_maxps)
            CHK("ANDPS", h_andps, sse_andps)
            CHK("ANDNPS", h_andnps, sse_andnps)
            CHK("ORPS",  h_orps,  sse_orps)
            CHK("XORPS", h_xorps, sse_xorps)
            CHK("ADDSS", h_addss, sse_addss)
            CHK("SUBSS", h_subss, sse_subss)
            CHK("MULSS", h_mulss, sse_mulss)
            CHK("DIVSS", h_divss, sse_divss)
            CHK("MINSS", h_minss, sse_minss)
            CHK("MAXSS", h_maxss, sse_maxss)
            CHK("UNPCKLPS", h_unpcklps, sse_unpcklps)
            CHK("UNPCKHPS", h_unpckhps, sse_unpckhps)
            CHK("MOVHLPS", h_movhlps, sse_movhlps)
            CHK("MOVLHPS", h_movlhps, sse_movlhps)
            CHK("MOVSS", h_movss, sse_movss_rr)
            CHK("SQRTPS", h_sqrtps, sse_sqrtps)
            CHK("RCPPS", h_rcpps, sse_rcpps)
            CHK("RSQRTPS", h_rsqrtps, sse_rsqrtps)
            CHK("SQRTSS", h_sqrtss, sse_sqrtss)
            CHK("RCPSS", h_rcpss, sse_rcpss)
            CHK("RSQRTSS", h_rsqrtss, sse_rsqrtss)
#undef CHK

            for (p = 0; p < 8; p++) {
                m = a; sse_cmpps(m.q, b.q, p);
                report("CMPPS", h_cmpps(a, b, p), m, a, b);
                m = a; sse_cmpss(m.q, b.q, p);
                report("CMPSS", h_cmpss(a, b, p), m, a, b);
            }

            for (imm = 0; imm < 256; imm++) {
                m = a; sse_shufps(m.q, b.q, (unsigned)imm);
                report("SHUFPS", h_shufps(a, b, imm), m, a, b);
            }

            /* MOVMSKPS and COMISS are not 128-bit results, so they are checked
               on their own terms rather than through report(). */
            {
                uint32_t hm = h_movmskps(a), mm = sse_movmskps(a.q);
                checks++;
                if (hm != mm) {
                    printf("  FAIL MOVMSKPS  host 0x%x model 0x%x\n", hm, mm);
                    fails++;
                }
            }
            {
                /* Only the three flags COMISS defines are compared: it leaves
                   OF/SF/AF cleared, but comparing the bits it does not write
                   would be comparing whatever popf inherited. */
                const uint32_t CMP = 0x0001u | 0x0004u | 0x0040u; /* CF PF ZF */
                CPU C;
                uint32_t hf, mf;
                memset(&C, 0, sizeof C);
                hf = h_comiss(a, b);
                sse_comiss(&C, a.q, b.q);
                mf = x86_eflags(&C);
                checks++;
                if ((hf & CMP) != (mf & CMP)) {
                    printf("  FAIL COMISS lane0 %08x vs %08x: flags 0x%03x "
                           "want 0x%03x\n", (uint32_t)a.q[0],
                           (uint32_t)b.q[0], mf & CMP, hf & CMP);
                    fails++;
                }
            }
        }
    }

    /*
     * Proof that this test can FAIL, on the two most plausible wrong models.
     *
     * 1. MINPS written with fminf() -- "the smaller of the two", the reading
     *    a maths library encourages. Hardware returns the SECOND operand
     *    whenever the comparison is false, so it yields the NaN when the
     *    source is one and +0.0 when comparing -0.0 with +0.0, and fminf does
     *    the opposite in both. (A plain `a < b ? a : b` is NOT a wrong model:
     *    that is precisely what MINPS computes, which is why it is not what is
     *    tested here.)
     * 2. ANDNPS read as `dest & ~src` instead of `~dest & src`.
     */
    {
        /* lane 0: NaN in the SOURCE; lane 1: -0.0 against +0.0 */
        V128 a = vb(0x3F800000u, 0x80000000u, 0x40000000u, 0x3F800000u);
        V128 b = vb(QNAN, 0x00000000u, 0x3F800000u, 0x40000000u);
        V128 host = h_minps(a, b), naive;
        float fa[4], fb[4], fn[4];
        int k, differ = 0;
        memcpy(fa, &a, 16); memcpy(fb, &b, 16);
        for (k = 0; k < 4; k++) fn[k] = fminf(fa[k], fb[k]);
        memcpy(&naive, fn, 16);
        checks++;
        if (host.q[0] == naive.q[0] && host.q[1] == naive.q[1]) {
            printf("  FAIL discriminator: the host's MINPS and an fminf() "
                   "model agree on the NaN/signed-zero case, so nothing above "
                   "could catch a model that took \"the smaller one\"\n");
            fails++;
        } else differ = 1;

        {
            V128 m = a, wrong = a;
            uint64_t w0, w1;
            sse_andnps(m.q, b.q);
            w0 = wrong.q[0] & ~b.q[0];      /* the WRONG order */
            w1 = wrong.q[1] & ~b.q[1];
            checks++;
            if (m.q[0] == w0 && m.q[1] == w1) {
                printf("  FAIL discriminator: ANDNPS and its reversed reading "
                       "agree on this corpus, so the operand order is "
                       "untested\n");
                fails++;
            }
        }
        (void)differ;
    }

    printf("test_sse: %d check(s), %d failure(s) -- %s\n", checks, fails,
           fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
