#include "x86_engine_x87_census.h"

#include "cpu.h"
#include "x87.h"

#include <lucent/cvar_c.h>
#include <lucent/log_c.h>

/* Shared by every guest thread; see the header for why one is enough. */
static X86pX87OpCensus g_census;
/* -1 until the cvar has been read; a CPU can be built before any engine
   configuration runs, so this resolves itself at the first attach rather than
   depending on an ordering between two call sites. */
static int g_armed = -1;

static const char *const kOpNames[X86P_X87_OPS] = {"FADD", "FSUB", "FMUL", "FDIV"};
/* The control word's own field encodings, in its own order. */
static const char *const kPrecisionNames[4] = {"24-bit (single)", "reserved", "53-bit (double)",
                                               "64-bit (extended)"};
static const char *const kRoundingNames[4] = {"nearest-even", "down", "up", "toward zero"};

static int armed(void) {
  if (g_armed < 0) {
    g_armed = lucent_cvar_flag("x87.census", 0);
    if (g_armed) {
      lucent_log_info("engine",
                      "x87.census=1: every x87 arithmetic operation is counted, "
                      "beside how many of them an encoding-level rule answers");
    }
  }
  return g_armed;
}

void x86_engine_x87_census_attach(struct X86pCpu *cpu) {
  if (!cpu || !armed()) {
    return;
  }
  x86p_x87_set_op_census(&cpu->x87, &g_census);
}

void x86_engine_x87_census_report(const char *tag) {
  unsigned long long total = 0;
  unsigned i;
  if (!armed()) {
    return;
  }
  for (i = 0; i < X86P_X87_OPS; i++) {
    total += (unsigned long long)g_census.total[i];
  }
  /*
   * A run that performed no x87 arithmetic and a census that was never
   * reached by one look identical in a table of zeroes, so this says which.
   */
  if (total == 0u) {
    lucent_log_info("engine",
                    "%sx87 census: armed, and no arithmetic operation reached "
                    "it -- either this run performed none or it never entered "
                    "guest code that does",
                    tag);
    return;
  }
  /*
   * THE MODE FIRST, because it can rule the rest out. An encoding-level rule
   * that only performs 80-bit round-to-nearest cannot answer one operation of
   * a title running at 53-bit precision, however ordinary its operands are --
   * and on a host with a real x87 unit the per-operand refusal columns below
   * are not measured at all, so this is the only column that says so.
   */
  for (i = 0; i < 4u; i++) {
    if (g_census.by_precision[i] != 0u) {
      lucent_log_info("engine", "%sx87 census: precision control %s on %llu operation(s) (%.1f%%)",
                      tag, kPrecisionNames[i], (unsigned long long)g_census.by_precision[i],
                      100.0 * (double)g_census.by_precision[i] / (double)total);
    }
  }
  for (i = 0; i < 4u; i++) {
    if (g_census.by_rounding[i] != 0u) {
      lucent_log_info("engine", "%sx87 census: rounding %s on %llu operation(s) (%.1f%%)", tag,
                      kRoundingNames[i], (unsigned long long)g_census.by_rounding[i],
                      100.0 * (double)g_census.by_rounding[i] / (double)total);
    }
  }
  for (i = 0; i < X86P_X87_OPS; i++) {
    const unsigned long long ops = (unsigned long long)g_census.total[i];
    const unsigned long long taken = (unsigned long long)g_census.taken[i];
    if (g_census.ordinary_measured) {
      /* The refusal columns print beside what was taken because they rank
         different pieces of work, and the remainder -- eligible, unanswered --
         is the work not done rather than a case the softfloat deserves. */
      lucent_log_info("engine",
                      "%sx87 census: %s %llu (%.1f%% of %llu): a rule took "
                      "%llu (%.1f%%); refused %llu for the control word, %llu "
                      "for a subnormal or special; %llu eligible and unanswered",
                      tag, kOpNames[i], ops, 100.0 * (double)ops / (double)total, total, taken,
                      ops ? 100.0 * (double)taken / (double)ops : 0.0,
                      (unsigned long long)g_census.refused_control[i],
                      (unsigned long long)g_census.refused_other[i],
                      ops - taken - (unsigned long long)g_census.refused_control[i] -
                          (unsigned long long)g_census.refused_other[i]);
    } else {
      lucent_log_info("engine",
                      "%sx87 census: %s %llu (%.1f%% of %llu); this host's "
                      "register file is not the ext80 encoding, so what a rule "
                      "would answer was not measured",
                      tag, kOpNames[i], ops, 100.0 * (double)ops / (double)total, total);
    }
  }
}
