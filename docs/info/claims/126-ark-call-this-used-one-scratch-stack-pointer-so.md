---
id: C126
kind: claim
status: holds
created: 2026-08-06
tags: graphics,vulkan,ark,rc-native
---

## Claim

ark_call_this used ONE scratch stack pointer, so a nested host->guest->host->guest call clobbered its caller's frame

## Evidence

Found by controlled experiment after the first hypothesis was refuted. igDxVisualContext::setupDrawing (libIGGfx 0x1002ead0) is a clean 28-instruction function -- no indirect JMP, no holes, one RET -- whose entire body calls vtable slot 47 then slot 186, both implemented by this backend. Its RET landed on 0x000001c0, four words out. First suspect was slot 47 re-entering the object's own vtable through a synthetic stub address; replacing that with a direct call to the engine body changed NOTHING, which is what pointed at the shared scratch stack. The real path is vk_open -> ark_call_this(engine helper 0x1002ca30) -> setupDrawing -> our slot 47 -> ark_call_this again, and every ark_call_this started its frame at the same g_call_sp, so the inner call laid its arguments over the outer call's frame. Fixed by reserving a fixed 8 KiB window below the current top per call and restoring the top afterwards. VERIFIED on a real --vk run: the invalid return disappears and the engine advances from setupDrawing to demanding slot 147.

## What would falsify it

A nesting depth beyond 32 (the 256 KiB arena divided by the 8 KiB window). The window is a FIXED reservation, not the callee's real frame size, which nothing here can know -- deeper nesting walks off the bottom of the arena. That would fault inside the arena rather than corrupt the engine's stack, but it would look like a new bug rather than this limit, so it is written down.
