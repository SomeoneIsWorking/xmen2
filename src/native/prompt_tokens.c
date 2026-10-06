#include "prompt_tokens.h"

#include "prompt_labels.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"

#define MAX_TOKENS 24u
#define MAX_SITES 8u

/* One token id, and whether resolving it produced one of the port's composed
   labels. Both counts are kept: an id asked for a thousand times of which
   none was ours is a different fact from one never asked for at all. */
struct Token {
  uint32_t id;
  unsigned long calls;
  unsigned long ours;
};

static struct Token g_tokens[MAX_TOKENS];
static unsigned g_n_tokens;
static unsigned long g_overflow;
static unsigned long g_calls, g_ours;
static uint32_t g_sites[MAX_SITES];
static unsigned long g_site_counts[MAX_SITES];
static unsigned g_n_sites;
static unsigned long g_site_overflow;

static void note_token(uint32_t id, int ours) {
  unsigned i;
  for (i = 0; i < g_n_tokens; i++) {
    if (g_tokens[i].id == id) {
      g_tokens[i].calls++;
      g_tokens[i].ours += (unsigned long)(ours != 0);
      return;
    }
  }
  if (g_n_tokens == MAX_TOKENS) {
    g_overflow++;
    return;
  }
  g_tokens[g_n_tokens].id = id;
  g_tokens[g_n_tokens].calls = 1;
  g_tokens[g_n_tokens].ours = (unsigned long)(ours != 0);
  g_n_tokens++;
}

static void note_site(uint32_t ret) {
  unsigned i;
  for (i = 0; i < g_n_sites; i++) {
    if (g_sites[i] == ret) {
      g_site_counts[i]++;
      return;
    }
  }
  if (g_n_sites == MAX_SITES) {
    g_site_overflow++;
    return;
  }
  g_sites[g_n_sites] = ret;
  g_site_counts[g_n_sites] = 1;
  g_n_sites++;
}

void x2_probe_004bd720(CPU *C) {
  const uint32_t ret = RD32(C->reg[kX86pEsp]);
  /* AN INTEGER, NOT A POINTER. Reading it as a string took a SIGSEGV at guest
     0x10d2; the ids a run actually asks for are 0xfffff004, 0xfffff014 and
     0xfffff015 for the composed action labels and small positive numbers for
     flat localized text. */
  const uint32_t token = RD32(C->reg[kX86pEsp] + 4u);
  int ours;

  g_calls++;
  x86_guest_body(C, "XMen2.exe", 0x004bd720u);
  /* The resolver returns the display string in EAX. Ours is the one guest
     buffer prompt_label_rewrite publishes. */
  ours =
      x2_prompt_label_buffer() && C->reg[kX86pEax] == x2_prompt_label_buffer();
  if (ours) {
    g_ours++;
    note_site(ret);
  }
  note_token(token, ours);
}

__attribute__((constructor)) static void x2_prompt_tokens_register(void) {
  x86_register_override("XMen2.exe", 0x004bd720, x2_probe_004bd720);
}

void x2_prompt_tokens_report(void) {
  unsigned i;
  x2_log_info("  Menu token resolver FUN_004bd720: %lu call(s) for %u distinct "
              "token id(s); %lu handed back one of the port's composed "
              "labels\n",
              g_calls, g_n_tokens, g_ours);
  if (!g_calls) {
    x2_log_info("        the resolver was never entered, so this run says "
                "NOTHING about which tokens become prompts.\n");
    return;
  }
  for (i = 0; i < g_n_tokens; i++) {
    x2_log_info("           token 0x%08x  x%lu, %lu composed by the port%s\n",
                g_tokens[i].id, g_tokens[i].calls, g_tokens[i].ours,
                g_tokens[i].ours ? ""
                                 : "  <- FLAT TEXT, no key cap and "
                                   "nothing a finger can press");
  }
  if (g_overflow) {
    x2_log_info("           %lu call(s) for token ids past the table\n",
                g_overflow);
  }
  for (i = 0; i < g_n_sites; i++) {
    x2_log_info("           our label consumed at 0x%08x  x%lu\n", g_sites[i],
                g_site_counts[i]);
  }
  if (g_site_overflow) {
    x2_log_info("           %lu consumer(s) past the table\n", g_site_overflow);
  }
}
