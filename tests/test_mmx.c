/*
 * The MMX lane model, against the CPU's OWN instructions.
 *
 * src/recomp/x86rt.h implements MMX as per-lane C over a uint64_t. Every one
 * of those is a place to get a detail wrong in a way nothing announces: which
 * way a pack saturates, whether a multiply's high half is signed, what a shift
 * count at or above the lane width does, which operand supplies the low half
 * of an interleave. libCriMovie's video decoder is 1106 such instructions, and
 * a wrong lane produces a picture -- just not the right one.
 *
 * So the oracle is not a table of numbers someone typed: it is the host's own
 * SSE2, running the SAME operation on the SAME bits. The host is x86-64, the
 * 128-bit forms have identical per-lane semantics to the 64-bit ones, and the
 * comparison is against silicon rather than against a second reading of the
 * manual.
 *
 * Where a 128-bit form is not a drop-in (the HIGH interleaves take bytes 8-15;
 * the packs take both halves from two registers), the operands are arranged so
 * that the low 64 bits of the SSE result are exactly what MMX would produce,
 * and the arrangement is spelled out at each site.
 */
#include <emmintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "x86rt.h"

/* The runtime's globals, so this links without the recompiled world. */
uint32_t g_imgbase = 0x10000000U;
uint32_t g_image_lo, g_image_hi;
__thread uint32_t g_fsbase, g_gsbase;

static int fails, checks;

static uint64_t lo64(__m128i v) {
  uint64_t out[2];
  memcpy(out, &v, 16);
  return out[0];
}

static __m128i pack2(uint64_t lo, uint64_t hi) {
  uint64_t in[2] = {lo, hi};
  __m128i v;
  memcpy(&v, in, 16);
  return v;
}

static void chk(const char *what, uint64_t got, uint64_t want) {
  checks++;
  if (got != want) {
    fails++;
    printf("  FAIL %-12s got 0x%016llx want 0x%016llx\n", what,
           (unsigned long long)got, (unsigned long long)want);
  }
}

/* Operand pairs chosen to exercise the cases that differ between a correct
   model and a plausible one: negatives, values that saturate in both
   directions, a high bit set in every lane, and zero. */
static const uint64_t A[] = {
    0x0001000200030004ULL, 0xFFFF80007FFF0001ULL, 0x8081FE7F01800000ULL,
    0x0000000000000000ULL, 0x7F7F7F7F80808080ULL, 0xDEADBEEFCAFEBABEULL,
};
static const uint64_t B[] = {
    0x0004000300020001ULL, 0x00017FFF8000FFFFULL, 0x7F01FF80007FFFFEULL,
    0xFFFFFFFFFFFFFFFFULL, 0x0102030405060708ULL, 0x123456789ABCDEF0ULL,
};
#define NPAIR (int)(sizeof A / sizeof A[0])

int main(void) {
  int i;
  printf("test_mmx: the lane model against the host CPU's SSE2\n");
  for (i = 0; i < NPAIR; i++) {
    __m128i a = pack2(A[i], 0), b = pack2(B[i], 0);

    chk("paddb", mmx_paddb(A[i], B[i]), lo64(_mm_add_epi8(a, b)));
    chk("paddw", mmx_paddw(A[i], B[i]), lo64(_mm_add_epi16(a, b)));
    chk("paddd", mmx_paddd(A[i], B[i]), lo64(_mm_add_epi32(a, b)));
    chk("psubb", mmx_psubb(A[i], B[i]), lo64(_mm_sub_epi8(a, b)));
    chk("psubw", mmx_psubw(A[i], B[i]), lo64(_mm_sub_epi16(a, b)));
    chk("psubd", mmx_psubd(A[i], B[i]), lo64(_mm_sub_epi32(a, b)));
    chk("paddusb", mmx_paddusb(A[i], B[i]), lo64(_mm_adds_epu8(a, b)));
    chk("paddusw", mmx_paddusw(A[i], B[i]), lo64(_mm_adds_epu16(a, b)));
    chk("psubusb", mmx_psubusb(A[i], B[i]), lo64(_mm_subs_epu8(a, b)));
    chk("psubusw", mmx_psubusw(A[i], B[i]), lo64(_mm_subs_epu16(a, b)));
    chk("paddsb", mmx_paddsb(A[i], B[i]), lo64(_mm_adds_epi8(a, b)));
    chk("paddsw", mmx_paddsw(A[i], B[i]), lo64(_mm_adds_epi16(a, b)));
    chk("psubsb", mmx_psubsb(A[i], B[i]), lo64(_mm_subs_epi8(a, b)));
    chk("psubsw", mmx_psubsw(A[i], B[i]), lo64(_mm_subs_epi16(a, b)));
    chk("pmullw", mmx_pmullw(A[i], B[i]), lo64(_mm_mullo_epi16(a, b)));
    chk("pmulhw", mmx_pmulhw(A[i], B[i]), lo64(_mm_mulhi_epi16(a, b)));
    chk("pmulhuw", mmx_pmulhuw(A[i], B[i]), lo64(_mm_mulhi_epu16(a, b)));
    chk("pmaddwd", mmx_pmaddwd(A[i], B[i]), lo64(_mm_madd_epi16(a, b)));
    chk("pavgb", mmx_pavgb(A[i], B[i]), lo64(_mm_avg_epu8(a, b)));
    chk("pavgw", mmx_pavgw(A[i], B[i]), lo64(_mm_avg_epu16(a, b)));
    chk("pcmpeqb", mmx_pcmpeqb(A[i], B[i]), lo64(_mm_cmpeq_epi8(a, b)));
    chk("pcmpeqw", mmx_pcmpeqw(A[i], B[i]), lo64(_mm_cmpeq_epi16(a, b)));
    chk("pcmpeqd", mmx_pcmpeqd(A[i], B[i]), lo64(_mm_cmpeq_epi32(a, b)));
    chk("pcmpgtb", mmx_pcmpgtb(A[i], B[i]), lo64(_mm_cmpgt_epi8(a, b)));
    chk("pcmpgtw", mmx_pcmpgtw(A[i], B[i]), lo64(_mm_cmpgt_epi16(a, b)));
    chk("pcmpgtd", mmx_pcmpgtd(A[i], B[i]), lo64(_mm_cmpgt_epi32(a, b)));
    chk("pminub", mmx_pminub(A[i], B[i]), lo64(_mm_min_epu8(a, b)));
    chk("pmaxub", mmx_pmaxub(A[i], B[i]), lo64(_mm_max_epu8(a, b)));
    chk("pminsw", mmx_pminsw(A[i], B[i]), lo64(_mm_min_epi16(a, b)));
    chk("pmaxsw", mmx_pmaxsw(A[i], B[i]), lo64(_mm_max_epi16(a, b)));

    /* LOW interleaves are a drop-in: the 128-bit form takes its lanes from
       the low half of each operand, which is the whole of an MMX one. */
    chk("punpcklbw", mmx_punpcklbw(A[i], B[i]), lo64(_mm_unpacklo_epi8(a, b)));
    chk("punpcklwd", mmx_punpcklwd(A[i], B[i]), lo64(_mm_unpacklo_epi16(a, b)));
    chk("punpckldq", mmx_punpckldq(A[i], B[i]), lo64(_mm_unpacklo_epi32(a, b)));
    /* HIGH interleaves are NOT: _mm_unpackhi takes bytes 8-15, while MMX
       takes bytes 4-7. Feeding the top halves in as the low halves makes
       the low-64 result identical. */
    chk("punpckhbw", mmx_punpckhbw(A[i], B[i]),
        lo64(_mm_unpacklo_epi8(pack2(A[i] >> 32, 0), pack2(B[i] >> 32, 0))));
    chk("punpckhwd", mmx_punpckhwd(A[i], B[i]),
        lo64(_mm_unpacklo_epi16(pack2(A[i] >> 32, 0), pack2(B[i] >> 32, 0))));
    chk("punpckhdq", mmx_punpckhdq(A[i], B[i]),
        lo64(_mm_unpacklo_epi32(pack2(A[i] >> 32, 0), pack2(B[i] >> 32, 0))));

    /* Packs take the destination's lanes first and the source's second.
       The 128-bit form takes all eight lanes of its FIRST operand, so
       putting the destination in the low half and the source in the high
       half of one register reproduces the 64-bit result exactly. */
    chk("packuswb", mmx_packuswb(A[i], B[i]),
        lo64(_mm_packus_epi16(pack2(A[i], B[i]), _mm_setzero_si128())));
    chk("packsswb", mmx_packsswb(A[i], B[i]),
        lo64(_mm_packs_epi16(pack2(A[i], B[i]), _mm_setzero_si128())));
    chk("packssdw", mmx_packssdw(A[i], B[i]),
        lo64(_mm_packs_epi32(pack2(A[i], B[i]), _mm_setzero_si128())));

    /* Shifts, including counts AT and PAST the lane width -- the case a
       model that just uses C's >> gets wrong (and where C itself is
       undefined), and the one MPEG code with a variable count reaches. */
    {
      static const int counts[] = {0, 1, 7, 15, 16, 31, 32, 63, 64, 200};
      int k;
      for (k = 0; k < (int)(sizeof counts / sizeof counts[0]); k++) {
        __m128i c = pack2((uint64_t)counts[k], 0);
        chk("psllw", mmx_psllw(A[i], (uint64_t)counts[k]),
            lo64(_mm_sll_epi16(a, c)));
        chk("pslld", mmx_pslld(A[i], (uint64_t)counts[k]),
            lo64(_mm_sll_epi32(a, c)));
        chk("psllq", mmx_psllq(A[i], (uint64_t)counts[k]),
            lo64(_mm_sll_epi64(a, c)));
        chk("psrlw", mmx_psrlw(A[i], (uint64_t)counts[k]),
            lo64(_mm_srl_epi16(a, c)));
        chk("psrld", mmx_psrld(A[i], (uint64_t)counts[k]),
            lo64(_mm_srl_epi32(a, c)));
        chk("psrlq", mmx_psrlq(A[i], (uint64_t)counts[k]),
            lo64(_mm_srl_epi64(a, c)));
        chk("psraw", mmx_psraw(A[i], (uint64_t)counts[k]),
            lo64(_mm_sra_epi16(a, c)));
        chk("psrad", mmx_psrad(A[i], (uint64_t)counts[k]),
            lo64(_mm_sra_epi32(a, c)));
      }
    }
  }

  /*
   * Proof that this test can FAIL. A comparison against silicon is worth
   * nothing if the harness would pass a wrong model, and the cheapest way to
   * be sure is to feed it one: an arithmetic shift written as a logical one,
   * which is the single most plausible mistake in the whole file and differs
   * only on negative lanes.
   */
  {
    uint64_t neg = 0xFFFF8000FFFF8000ULL;
    uint64_t right = mmx_psraw(neg, 4);
    uint64_t wrong = mmx_psrlw(neg, 4);
    checks++;
    if (right == wrong) {
      printf("  FAIL discriminator: an arithmetic and a logical shift of "
             "0x%016llx agree, so this test could not tell a wrong model "
             "from a right one\n",
             (unsigned long long)neg);
      fails++;
    }
  }

  printf("test_mmx: %d check(s), %d failure(s) -- %s\n", checks, fails,
         fails ? "FAILED" : "PASSED");
  return fails ? 1 : 0;
}
