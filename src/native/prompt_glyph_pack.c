#include "prompt_glyph_pack.h"

#include <stdlib.h>

int prompt_glyph_pack_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("X2_PROMPT_GLYPHS");
        if (!value) value = getenv("X2_PAD_GLYPHS"); /* old launcher alias */
        enabled = value && *value && *value != '0';
    }
    return enabled;
}
