/*
 * Native prompt art at the exact Alchemy text-batch boundary.
 *
 * The live D3D callsite probe separated two draws made while prompt quads were
 * pending: the 57-primitive draw returns into drawIndexed at 0x10035666, while
 * the stock text plane returns into drawNonIndexed at 0x10035489. Ghidra names
 * the owner Gap::Gfx::igDxVisualContext::drawNonIndexed at libIGGfx
 * 0x100352d0. That body calls updateContextState at 0x10034e60 before it
 * submits to D3D. The outer override brackets the semantic non-indexed batch;
 * the nested override super-calls the engine finalizer, then draws the SVG
 * with the now-final world/view/projection matrices. Control returns to
 * drawNonIndexed, whose stock ASCII lands on top of the keycap background.
 */
#include "prompt_glyph_batch.h"

#include "gpu_prompt_glyphs.h"
#include "prompt_glyph_quads.h"
#include "ui_transform.h"
#include "x86rt_native.h"

#include <stdio.h>
#include "guest_body.h"

static unsigned g_nonindexed_depth;
static unsigned long g_calls, g_finalizer_calls, g_nested_finalizers;
static unsigned long g_with_prompts, g_drawn;
static unsigned long g_transform_refused, g_gpu_refused, g_unfinalized_refused;


void x2_prompt_glyph_batch_draw_nonindexed(CPU *C)
{
    unsigned count;

    g_calls++;
    g_nonindexed_depth++;
    x86_guest_body(C, "libIGGfx.dll", 0x100352d0u);
    g_nonindexed_depth--;

    /* updateContextState is the only evidenced point where this batch has a
       finalized transform. If the original body returned without a nested
       finalizer consuming the harvest, refuse it here; leaving it pending
       would attach this text to a later, unrelated draw. */
    (void)x2_prompt_quads(&count);
    if (count) {
        g_unfinalized_refused += count;
        x2_prompt_quads_consume();
    }
}

void x2_prompt_glyph_batch_update_context_state(CPU *C)
{
    uint32_t context = C->ecx;
    float mvp[16];
    unsigned count;

    x86_guest_body(C, "libIGGfx.dll", 0x10034e60u);
    g_finalizer_calls++;
    if (!g_nonindexed_depth) return;
    g_nested_finalizers++;
    (void)x2_prompt_quads(&count);
    if (count) {
        g_with_prompts++;
        if (!x2_ui_transform_current(context, mvp)) {
            g_transform_refused += count;
            x2_prompt_quads_consume();
        } else if (!gpu_prompt_glyphs_render(mvp)) {
            g_gpu_refused += count;
            x2_prompt_quads_consume();
        } else {
            g_drawn += count;
        }
    }
}

__attribute__((constructor))
static void x2_prompt_glyph_batch_register(void)
{
    x86_register_override("libIGGfx.dll", 0x100352d0u,
                          x2_prompt_glyph_batch_draw_nonindexed);
    x86_register_override("libIGGfx.dll", 0x10034e60u,
                          x2_prompt_glyph_batch_update_context_state);
}

void x2_prompt_glyph_batch_report(void)
{
    printf("  Alchemy non-indexed text boundary: %lu draw call(s), %lu "
           "state finalizer call(s) total and %lu nested in this boundary; "
           "%lu nested finalizer(s) carried prompt quads; %lu glyph quad(s) "
           "submitted, %lu refused because the matching engine transform "
           "was unavailable, "
           "%lu refused by the GPU path, %lu refused because the draw "
           "returned without consuming them at a finalized boundary\n",
           g_calls, g_finalizer_calls, g_nested_finalizers, g_with_prompts,
           g_drawn, g_transform_refused, g_gpu_refused,
           g_unfinalized_refused);
    if (!g_calls)
        printf("        ZERO calls at libIGGfx.dll 0x100352d0 -- the "
               "engine's non-indexed draw boundary was not reached.\n");
}
