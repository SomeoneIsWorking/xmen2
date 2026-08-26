---
id: C271
kind: claim
status: holds
created: 2026-08-26
tags: graphics,svg,prompts,libIGGfx
depends: src/native/prompt_glyph_draw.c#x2_override_005ee780, src/native/prompt_glyph_draw.c#x2_override_005ee400, src/native/prompt_glyph_batch.c#x2_prompt_glyph_batch_draw_nonindexed, src/native/prompt_glyph_batch.c#x2_prompt_glyph_batch_update_context_state, src/native/ui_transform.c#x2_ui_transform_compute_matrix, src/native/prompt_glyph_metrics.c#x2_prompt_glyph_publish_metrics, src/gpu/gpu_prompt_glyphs.c#gpu_prompt_glyphs_render, tools/render_prompt_glyphs.py#emit_header, tools/prepare_native_assets.py#prepare
reconfirmed: 2026-08-26
verified_at: 2026-08-26 23:22:15
---

## Claim

Native prompt SVGs render through Alchemy's own finalized non-indexed text batch while the shipped font pixels remain untouched

## Evidence

Ghidra: libIGGfx igDxVisualContext::drawNonIndexed 0x100352d0 calls updateContextState 0x10034e60 before DrawPrimitive (return 0x10035489), but only when the primitive count is positive. The native string transaction therefore queues each prompt rectangle and super-calls FUN_005ee400 with x1=x0/y1=y0: no stock pixels, while the engine still receives its vertex/finalizer event. The outer/nested overrides retain the evidenced ordering and discard an unmatched batch rather than carrying it forward. scratch/logs/svg-final-unbounded.log records 1,188 keycap quads in 99 batches with zero refusals/desyncs, and scratch/screenshots/svg-final-unbounded.png visibly encloses stock ENTER. The separate headless, timed-SILENT, X2_UNPACED pure-controller run at scratch/logs/svg-pad-final-unbounded.log records 1,073 one-codepoint strings, intercepted emitters, matching nested finalizers and GPU submissions, with zero unavailable-byte, colour, queue, desync, transform, cross-context, GPU or unfinalized-boundary refusals. scratch/screenshots/svg-pad-unbounded.png visibly shows the native A icon beside CONTINUE. The CMake direct build regenerated a deliberately removed atlas header from all shared SVG dependencies. tools/prepare_native_assets.py no longer reads or writes font files.

## What would falsify it

Any prompt SVG is absent/misaligned on a real run; a prompt string partially mixes native and stock fallback; a batch reports desync, colour/capacity, transform, cross-context, GPU, or unfinalized/orphan refusal; an occupied retail codepoint is still used natively; a direct build consumes a missing/stale atlas; the default asset preparation writes a font file; or later RE contradicts the drawNonIndexed/updateContextState call order.

## Re-confirmed 2026-08-26

scratch/logs/svg-pad-final-unbounded.log records 1,073 pure glyph strings, nested Alchemy finalizers and GPU submissions with every atomicity/transform/orphan refusal at zero; the headless capture shows the A SVG beside CONTINUE, and all 114 CTests pass.
