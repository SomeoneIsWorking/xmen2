/* Presentation for the action labels produced by XMen2.exe FUN_00619e30.
 *
 * The retail function returns "[NAME]". A pad name is already a complete
 * picture and loses the brackets. A keyboard name is composited over the
 * shared blank keycap without baking a codepoint per possible binding:
 *
 *   left, repeated middle strips, repeated negative-advance rewinds,
 *   the retail binding name, right
 *
 * The background glyphs draw first, the rewinds return the text pen, and the
 * unchanged stock letters draw over them. A rebind therefore changes the
 * prompt immediately and consumes four codepoints for the whole keyboard.
 */
#include "prompt_labels.h"

#include "guest_heap.h"
#include "pad_glyph_codes.h"
#include "prompt_glyph_pack.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <string.h>

#define LABEL_BUFFER_BYTES 512u
#define MAX_RETAIL_LABEL   127u

static unsigned long g_unchanged, g_pad_labels, g_keycap_labels;
static unsigned long g_buffer_failures;
static uint32_t g_styled_label;

/* WHO asks for these labels? The composed label is handed back as a return
   value, and nothing in this file knows whether the caller draws it, stores
   it or throws it away -- which is exactly the gap that made "2,264 labels
   composed, 0 prompt codepoints at the glyph loop" (C267) unreadable: a
   label READ is not a label DRAWN. Recording the return address at entry
   names the consumer from a real run, the same way the getTexture probe
   named the glyph loop. Always on: it is a handful of compares against a
   tiny table, and a census nobody arms is a census nobody has. */
#define MAX_LABEL_SITES 16
static uint32_t g_sites[MAX_LABEL_SITES];
static unsigned long g_site_counts[MAX_LABEL_SITES];
static unsigned g_n_sites;
static unsigned long g_site_overflow;

/* One hop further out. FUN_004bd720 is the token resolver that asks for the
   label: it takes a token, calls FUN_00619e30 for its display string and
   RETURNS that pointer to its own caller (L_004bd7ff). Knowing who consumes
   that return is what separates "our label is never drawn" from "our label
   is drawn somewhere we have not looked" -- the question C267 left open.
   Recorded only when the resolver actually hands back OUR buffer, so a hit
   here is the port's own bytes leaving the resolver, not merely traffic. */
static uint32_t g_resolver_sites[MAX_LABEL_SITES];
static unsigned long g_resolver_counts[MAX_LABEL_SITES];
static unsigned g_n_resolver_sites;
static unsigned long g_resolver_calls, g_resolver_ours, g_resolver_overflow;

static void note_resolver(uint32_t ret)
{
    unsigned i;
    for (i = 0; i < g_n_resolver_sites; i++)
        if (g_resolver_sites[i] == ret) { g_resolver_counts[i]++; return; }
    if (g_n_resolver_sites == MAX_LABEL_SITES) { g_resolver_overflow++; return; }
    g_resolver_sites[g_n_resolver_sites] = ret;
    g_resolver_counts[g_n_resolver_sites] = 1;
    g_n_resolver_sites++;
}

void fn_XMen2_004bd720(CPU *C);

void x2_probe_004bd720(CPU *C)
{
    uint32_t ret = RD32(C->esp);
    g_resolver_calls++;
    fn_XMen2_004bd720(C);
    /* The resolver returns the display string in EAX. Ours is the one guest
       buffer prompt_label_rewrite publishes. */
    if (g_styled_label && C->eax == g_styled_label) {
        g_resolver_ours++;
        note_resolver(ret);
    }
}

static void note_caller(uint32_t ret)
{
    unsigned i;
    for (i = 0; i < g_n_sites; i++)
        if (g_sites[i] == ret) { g_site_counts[i]++; return; }
    if (g_n_sites == MAX_LABEL_SITES) { g_site_overflow++; return; }
    g_sites[g_n_sites] = ret;
    g_site_counts[g_n_sites] = 1;
    g_n_sites++;
}

static int pad_glyph_byte(uint8_t value)
{
    return value >= X2_PAD_GLYPH_FIRST && value <= X2_PAD_GLYPH_LAST;
}

enum PromptLabelStyle prompt_label_rewrite(const uint8_t *input,
                                           uint8_t *output, size_t capacity)
{
    size_t length, name_length, i, at;
    size_t units;

    if (!input || !output || !capacity) return PROMPT_LABEL_UNCHANGED;
    length = strlen((const char *)input);
    if (length == 3u && input[0] == '[' && pad_glyph_byte(input[1]) &&
        input[2] == ']') {
        if (capacity < 2u) return PROMPT_LABEL_UNCHANGED;
        output[0] = input[1];
        output[1] = 0;
        return PROMPT_LABEL_PAD_GLYPH;
    }
    if (length < 3u || input[0] != '[' || input[length - 1u] != ']')
        return PROMPT_LABEL_UNCHANGED;
    name_length = length - 2u;
    units = name_length;
    if (name_length > 63u ||
        (name_length == 3u && memcmp(input + 1u, "???", 3u) == 0))
        return PROMPT_LABEL_UNCHANGED;
    for (i = 0; i < name_length; i++) {
        uint8_t value = input[i + 1u];
        if (value < 0x20u || value > 0x7eu)
            return PROMPT_LABEL_UNCHANGED;
    }
    if (3u + units * 2u + name_length > capacity)
        return PROMPT_LABEL_UNCHANGED;

    at = 0;
    output[at++] = X2_KEYCAP_GLYPH_LEFT;
    for (i = 0; i < units; i++) output[at++] = X2_KEYCAP_GLYPH_MIDDLE;
    for (i = 0; i < units; i++) output[at++] = X2_KEYCAP_GLYPH_REWIND;
    memcpy(output + at, input + 1u, name_length);
    at += name_length;
    output[at++] = X2_KEYCAP_GLYPH_RIGHT;
    output[at] = 0;
    return PROMPT_LABEL_KEYCAP;
}

void fn_XMen2_00619e30(CPU *C);

void x2_override_00619e30(CPU *C)
{
    uint8_t retail[MAX_RETAIL_LABEL + 1u];
    uint8_t styled[LABEL_BUFFER_BYTES];
    uint32_t out;
    size_t length;
    enum PromptLabelStyle style;

    /* Before the super-call: the retail body pops its own return address. */
    note_caller(RD32(C->esp));
    fn_XMen2_00619e30(C);
    out = C->eax;
    if (!out || !prompt_glyph_pack_enabled()) {
        g_unchanged++;
        return;
    }
    for (length = 0; length < MAX_RETAIL_LABEL; length++) {
        retail[length] = RD8(out + (uint32_t)length);
        if (!retail[length]) break;
    }
    if (length == MAX_RETAIL_LABEL) {
        g_unchanged++;
        return;
    }
    style = prompt_label_rewrite(retail, styled, sizeof styled);
    if (style == PROMPT_LABEL_UNCHANGED) {
        g_unchanged++;
        return;
    }
    if (!g_styled_label) g_styled_label = guest_malloc(LABEL_BUFFER_BYTES);
    if (!g_styled_label) {
        g_buffer_failures++;
        return;
    }
    length = strlen((const char *)styled) + 1u;
    for (size_t i = 0; i < length; i++)
        WR8(g_styled_label + (uint32_t)i, styled[i]);
    C->eax = g_styled_label;
    if (style == PROMPT_LABEL_PAD_GLYPH) g_pad_labels++;
    else g_keycap_labels++;
}

__attribute__((constructor))
static void x2_prompt_labels_register_override(void)
{
    x86_register_override("XMen2.exe", 0x00619e30, x2_override_00619e30);
    x86_register_override("XMen2.exe", 0x004bd720, x2_probe_004bd720);
}

void prompt_labels_report(void)
{
    static int done;
    unsigned i;
    if (done++) return;
    printf("  Prompt labels: %lu keycap, %lu pad, %lu unchanged; %lu guest "
           "buffer allocation failure(s)\n", g_keycap_labels, g_pad_labels,
           g_unchanged, g_buffer_failures);
    if (!g_n_sites) {
        printf("        asked for by NOBODY in this run -- 0 call(s) "
               "reached FUN_00619e30, so nothing here is evidence about "
               "where a label goes.\n");
        return;
    }
    printf("        asked for from %u distinct call site(s)%s:\n", g_n_sites,
           g_site_overflow ? " (TABLE FULL -- later sites went unrecorded)"
                           : "");
    for (i = 0; i < g_n_sites; i++)
        printf("           return to 0x%08x  x%lu\n",
               g_sites[i], g_site_counts[i]);
    if (g_site_overflow)
        printf("           %lu call(s) from sites past the table\n",
               g_site_overflow);
    printf("        token resolver FUN_004bd720: %lu call(s), %lu of which "
           "handed OUR buffer back to the caller\n",
           g_resolver_calls, g_resolver_ours);
    if (!g_resolver_calls)
        printf("           the resolver was never entered, so this run says "
               "NOTHING about where a resolved label goes.\n");
    else if (!g_resolver_ours)
        printf("           the resolver ran but never returned our buffer -- "
               "the label we compose is not what it hands out.\n");
    for (i = 0; i < g_n_resolver_sites; i++)
        printf("           consumed at 0x%08x  x%lu\n",
               g_resolver_sites[i], g_resolver_counts[i]);
    if (g_resolver_overflow)
        printf("           %lu consumer(s) past the table\n",
               g_resolver_overflow);
}
