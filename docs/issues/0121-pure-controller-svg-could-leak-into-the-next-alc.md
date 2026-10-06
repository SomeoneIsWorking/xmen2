---
id: 121
title: Pure controller SVG could leak into the next Alchemy draw when its retail emitter was skipped
status: resolved
symptom: A one-codepoint controller prompt had no stock vertex, so drawNonIndexed could skip updateContextState and leave the native SVG queued for an unrelated later draw.
state_items: S004
tags: svg,prompts,alchemy,renderer
created: 2026-08-26
updated: 2026-08-26
---

Static review of the native SVG path found that FUN_005ee400 was bypassed for private codepoints. A keycap still carried ASCII vertices, hiding the defect, but a controller label is exactly one private codepoint. libIGGfx drawNonIndexed checks for a positive primitive count before its nested updateContextState finalizer, so suppressing that only emitter call could orphan the queued SVG. The same review found whole-string queue overflow was not atomic, transform validity survived unreadable/context-changing computeMatrix calls, occupied retail codepoints were still intercepted, and the atlas header had no direct-build generation dependency.

The correct boundary preserves the engine event: preflight the whole string, queue each native rectangle, and super-call FUN_005ee400 with x1=x0 and y1=y0. This produces no stock pixels while retaining the retail vertex/batch/finalizer semantics. Batch and transform owners discard rather than carry state across an unmatched boundary or visual context.

### Resolution (2026-08-26)
Fixed at the evidenced ownership boundary. FUN_005ee780 now preflights codepoint availability, arg2+8 batch colour, and whole-string queue capacity; each native FUN_005ee400 call queues its original rectangle then super-calls with x1=x0/y1=y0. The batch owner discards any quads left after an outer draw returns without its nested finalizer, and the transform cache invalidates before unreadable output or visual-context changes. Occupied retail bytes globally disable native naming/composition. CMake now regenerates a missing/stale atlas from the complete shared dependency set. Focused production-boundary tests, the Clang build, and all 114 CTests pass with dummy audio and unbounded scheduling (fmv_decode skipped by its existing data gate). scratch/logs/svg-pad-final-unbounded.log reports 1,073 pure one-codepoint strings through 1,073 intercepted emitters, matching nested finalizers, and GPU submissions with every new refusal/orphan counter at zero; scratch/screenshots/svg-pad-unbounded.png shows the aligned A icon.
