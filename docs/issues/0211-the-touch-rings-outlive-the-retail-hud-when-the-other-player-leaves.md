---
id: 211
title: the touch rings outlive the retail HUD when the other player leaves
status: open
symptom: "In a network game the host's screen shows its portrait, potion and menu rings with no icons in them; the client's HUD is complete."
state_items: S023
tags: lan,hud,touch
created: 2026-11-12
updated: 2026-11-12
---

# 0211 — the touch rings outlive the retail HUD when the other player leaves

## Observed

Run 39 on 2026-11-12 (`scratch/lan-pub/measure.py`, the host pressing
Continue through the opening). The client's last screenshot showed the four
portraits, both potions and the menu icons. The host's showed the rings empty,
with only the settings icon and the gameplay buttons drawn. Run 43 showed the
same thing, and its host frame also lacked the vitals bar and the selection
rings under the heroes.

## Narrowed

It is not free play. In runs 39 and 43 the host's last screenshot came back
3 s after the client had shut down; the client's came back in 1.3 s.

- Run 44: the client's game was stopped for 1.5 s mid-session. Eight host
  frames taken during the stop all showed the full HUD.
- Run 45: the client exited. The host's frame 1 s later showed the empty rings
  over a world with no vitals bar or selection rings. From 3 s on the overlay
  was gone too, and then the host showed the drop popup.

So for up to about 2 s after the other player leaves, CHud::draw still runs and
records its elements (the touch rings follow those masks,
`src/native/hud_draw_runtime.cpp:hud_party_draw`), but nothing it draws is
visible. CHud::draw passes constant 1.0 colours, so this is not a retail fade.
Next: find where the frame's GUI and selection draws go in that window
(render target, viewport, or a skipped pass) and publish the masks from what
reaches the screen.
