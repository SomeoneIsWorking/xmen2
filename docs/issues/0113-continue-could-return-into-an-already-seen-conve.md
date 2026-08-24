---
id: 113
title: Continue could return into an already-seen conversation without a scoped resume owner
status: resolved
symptom: Loading through Continue can leave the restored authored cutscene or conversation waiting instead of advancing to the saved playable state
tags: pc,native,continue,conversation,cutscene,scripts
created: 2026-08-24
updated: 2026-08-24
---

## Cause

The Continue transaction and authored-conversation policy were independent. A successful map return had no bounded authority to advance deterministic restored responses or their script-owned timed waits; a global skip would be unsafe because unrelated dialogue and waits use the same primitives.

## Resolution

Continue arms resume only after retail accepts the exact load and the map returns successfully. The policy expires after ten guest-clock seconds before classifying later conversations, stops at choices, and carries hidden gaps only under an exact script-context owner. `waittimed` shortens that owner only; foreign, malformed and expired waits super-call retail. Normal/NDEBUG policy and production-ABI tests pin these boundaries.
