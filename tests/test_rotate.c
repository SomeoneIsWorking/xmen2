/*
 * ROL/ROR/RCL/RCR, against the CPU's OWN rotate instructions.
 *
 * x86rt.h models these in C. Four things are easy to get wrong and none of
 * them announces itself: the count mask (5 bits, then modulo the width -- or
 * modulo width+1 for the through-carry forms, because the carry is part of the
 * rotated quantity), the fact that a masked count of zero must leave the FLAGS
 * alone as well as the value, that a rotate writes ONLY CF and OF, and RCR's
 * OF, which is taken from the operand BEFORE the rotate.
 *
 * The host is x86-64 and runs the real instructions, so the oracle is silicon
 * rather than a second reading of the manual. Only 8/16/32-bit forms are
 * tested, because those are the only widths a 32-bit guest can encode.
 *
 * OF for a count other than 1 is architecturally UNDEFINED, so it is compared
 * only where the count is 1. That exclusion is the honest one: comparing it
 * everywhere would fail on a correct model, and comparing it nowhere would
 * miss the case the architecture actually defines.
 */
#include <stdint.h>
#include <stdio.h>

#include "x86rt.h"

/* The runtime's globals, so this links without the recompiled world. */
uint32_t g_imgbase = 0x10000000U;
uint32_t g_image_lo, g_image_hi;
__thread uint32_t g_fsbase, g_gsbase;

static int fails, checks;

#define CF 0x0001u
#define OF 0x0800u

/*
 * Run the real instruction. The flags going IN matter (RCL/RCR consume CF), so
 * the whole EFLAGS word is loaded with popf and read back with pushf.
 */
#define HOST_ROT(MN, SUF, REG, TYPE)                                           \
  static uint32_t host_##MN##_##SUF(uint32_t v, uint32_t c, uint32_t *fl) {    \
    uint64_t f = *fl | 2u;                                                     \
    TYPE val = (TYPE)v;                                                        \
    __asm__ volatile("push %[fin]\n\t"                                         \
                     "popfq\n\t" #MN " %%cl, %[v]\n\t"                         \
                     "pushfq\n\t"                                              \
                     "pop %[fout]"                                             \
                     : [v] "+" REG(val), [fout] "=r"(f)                        \
                     : [fin] "r"(f), "c"((uint8_t)c)                           \
                     : "cc");                                                  \
    *fl = (uint32_t)f;                                                         \
    return (uint32_t)val;                                                      \
  }

HOST_ROT(rolb, 8, "q", uint8_t)
HOST_ROT(rorb, 8, "q", uint8_t)
HOST_ROT(rclb, 8, "q", uint8_t)
HOST_ROT(rcrb, 8, "q", uint8_t)
HOST_ROT(rolw, 16, "r", uint16_t)
HOST_ROT(rorw, 16, "r", uint16_t)
HOST_ROT(rclw, 16, "r", uint16_t)
HOST_ROT(rcrw, 16, "r", uint16_t)
HOST_ROT(roll, 32, "r", uint32_t)
HOST_ROT(rorl, 32, "r", uint32_t)
HOST_ROT(rcll, 32, "r", uint32_t)
HOST_ROT(rcrl, 32, "r", uint32_t)

typedef uint32_t (*HostFn)(uint32_t, uint32_t, uint32_t *);

static const struct {
  const char *name;
  int kind, w;
  HostFn fn;
} CASES[] = {
    {"rol.b", X86_ROL, 1, host_rolb_8},  {"ror.b", X86_ROR, 1, host_rorb_8},
    {"rcl.b", X86_RCL, 1, host_rclb_8},  {"rcr.b", X86_RCR, 1, host_rcrb_8},
    {"rol.w", X86_ROL, 2, host_rolw_16}, {"ror.w", X86_ROR, 2, host_rorw_16},
    {"rcl.w", X86_RCL, 2, host_rclw_16}, {"rcr.w", X86_RCR, 2, host_rcrw_16},
    {"rol.l", X86_ROL, 4, host_roll_32}, {"ror.l", X86_ROR, 4, host_rorl_32},
    {"rcl.l", X86_RCL, 4, host_rcll_32}, {"rcr.l", X86_RCR, 4, host_rcrl_32},
};

static const uint32_t VALS[] = {
    0x00000000u, 0xFFFFFFFFu, 0x00000001u, 0x80000000u,
    0x12345678u, 0xDEADBEEFu, 0x0000FF01u, 0x80000001u,
};
/* 0 and the multiples of the width are the ones a plain shift-pair model gets
   wrong; 17 and 33 are past the 5-bit mask and past width+1. */
static const uint32_t COUNTS[] = {
    0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 255,
};

int main(void) {
  unsigned ci, vi, ni, fi;
  printf("test_rotate: the rotate model against the host CPU's own "
         "instructions\n");

  for (ci = 0; ci < sizeof CASES / sizeof CASES[0]; ci++) {
    uint32_t mask = x86_mask(CASES[ci].w);
    for (vi = 0; vi < sizeof VALS / sizeof VALS[0]; vi++) {
      for (ni = 0; ni < sizeof COUNTS / sizeof COUNTS[0]; ni++) {
        /* Both carry-in states, and a couple of unrelated flags set,
           so "the model clobbered ZF/SF" is caught too. */
        static const uint32_t FIN[] = {0x0002u, 0x0003u, 0x00C6u, 0x00C7u};
        for (fi = 0; fi < sizeof FIN / sizeof FIN[0]; fi++) {
          uint32_t hf = FIN[fi], mf = FIN[fi];
          uint32_t hv = CASES[ci].fn(VALS[vi] & mask, COUNTS[ni], &hf);
          uint32_t mv = x86_rotate(VALS[vi], COUNTS[ni], CASES[ci].w,
                                   CASES[ci].kind, &mf);
          uint32_t cmp = CF | 0x0004u | 0x0040u | 0x0080u; /* CF PF ZF SF */
          if ((COUNTS[ni] & 31u) == 1u)
            cmp |= OF;
          checks++;
          if ((hv & mask) != (mv & mask)) {
            printf("  FAIL %-6s 0x%08x rot %u (fin 0x%03x): got "
                   "0x%08x want 0x%08x\n",
                   CASES[ci].name, VALS[vi] & mask, COUNTS[ni], FIN[fi],
                   mv & mask, hv & mask);
            fails++;
          } else if ((hf & cmp) != (mf & cmp)) {
            printf("  FAIL %-6s 0x%08x rot %u (fin 0x%03x): flags "
                   "0x%03x want 0x%03x (compared 0x%03x)\n",
                   CASES[ci].name, VALS[vi] & mask, COUNTS[ni], FIN[fi],
                   mf & cmp, hf & cmp, cmp);
            fails++;
          }
        }
      }
    }
  }

  /*
   * Proof that this test can FAIL, on the single most plausible wrong model:
   * RCL written as ROL, i.e. forgetting that the carry is part of the
   * rotated quantity. They must disagree on a value whose top bit and carry
   * differ -- if they did not, none of the checks above could tell a
   * through-carry rotate from a plain one.
   */
  {
    uint32_t hf = 0x0003u, mf = 0x0003u; /* CF set */
    uint32_t host = host_rcll_32(0x40000000u, 1, &hf);
    uint32_t wrong = x86_rotate(0x40000000u, 1, 4, X86_ROL, &mf);
    checks++;
    if (host == wrong && (hf & (CF | OF)) == (mf & (CF | OF))) {
      printf("  FAIL discriminator: the host's RCL and a model that "
             "rotated WITHOUT the carry agree on 0x40000000 with CF set "
             "(both 0x%08x), so the comparison above could pass a model "
             "that ignored the carry entirely\n",
             host);
      fails++;
    }
  }

  printf("test_rotate: %d check(s), %d failure(s) -- %s\n", checks, fails,
         fails ? "FAILED" : "PASSED");
  return fails ? 1 : 0;
}
