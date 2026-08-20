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
}

void prompt_labels_report(void)
{
    static int done;
    if (done++) return;
    printf("  Prompt labels: %lu keycap, %lu pad, %lu unchanged; %lu guest "
           "buffer allocation failure(s)\n", g_keycap_labels, g_pad_labels,
           g_unchanged, g_buffer_failures);
}
