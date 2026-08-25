---
id: 113
title: Continue could return into an already-seen conversation without a scoped resume owner
status: resolved
symptom: Loading through Continue can leave the restored authored cutscene or conversation waiting instead of advancing to the saved playable state
tags: pc,native,continue,conversation,cutscene,scripts
created: 2026-08-24
updated: 2026-08-24
---

## Superseded diagnosis

The Continue transaction and authored-conversation policy were independent. A successful map return had no bounded authority to advance deterministic restored responses or their script-owned timed waits; a global skip would be unsafe because unrelated dialogue and waits use the same primitives.

The response half was valid, but treating the script's timed waits as something
to accelerate was not. The unit tests proved only that the override was scoped;
they did not prove that removing the owned delay preserved the script's
prerequisites.

## Regression and correction

The first default-path user run after `82bdf13` recorded physical Escape and
advanced the first conversation from visible (`0x13`) to ending (`0x18`). The
adjacent conversation then entered `0x10` with no visible line, the exact issue
#83 softlock signature. In the last live-working Escape run, retail timing put
`nightcrawler_spawn`, `nightcrawler_walk`, and the second conversation on
separate frames; the second conversation selected its line and launched
`conv_0020b_end`. The added `waittimed` override erased those actor/movement
delays and let the adjacent conversation run before its speaker prerequisites
were ready.

Continue arms resume only after retail accepts the exact load and the map
returns successfully. The policy expires after ten guest-clock seconds before
classifying later conversations, stops at choices, and retires at the retail
hidden/ending boundary rather than claiming the script which runs afterward.

The native `waittimed` override and its ABI test have now been removed. Manual
Escape and Continue may select a deterministic visible response through the
retail conversation path, but every script wait remains retail-owned. This
entry stays investigating until the corrected default Continue path is live
verified through the resumed conversation and playable-state handoff.

### Resolved (2026-08-25)

Root cause found by live comparison. Manual menu-Continue ends with
`current player index 0` and a resolved hero handle; boot-Continue ended with
index -1 and every handle UNRESOLVED -- exactly issue #83's speaker-collision
precondition, so the replayed adjacent conversation collided on the seen-line
bitmap and never showed a line. The menu lifecycle between the Show intercept
and the LOAD SUCCESSFUL ack clears CPadManager, and the payload keys its party
writes off that player; the ack now re-selects the primary player before the
payload deserializes. boot-continue 12/12 includes: 0020b STARTED visible with
a line, conv_0020b_end launched, controls unlocked. Residual, cataloged
separately as #117: a controller first seen after the load is recorded but
not polled.
