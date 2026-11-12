---
id: 211
title: the touch rings outlive the retail HUD when the other player leaves
status: resolved
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

It is not free play. The host's shot came back after the client had shut
down. Stopping the client for 1.5 s mid-session left all eight host shots
complete (run 44). With the client gone, the host's next shot showed the empty
rings in every run from 45 to 52.

## Cause

When the other player leaves, the host stops calling CHud's party, vitals and
inventory draws. In run 52 that happened at frame 519298, the same frame the
screenshot captured. The GUI layers were still rendered three times per
frame, and the GUI flag at `GUI+0x85c30` and the sprite pool were unchanged.
CHud simply drew nothing.

The overlay is drawn at the end of each frame. `src/ui/rmlui_ui.cpp` drew it
whenever `x2_gameplay_control_state` was active, which is true for one frame
after the last HUD draw so that input is not dropped between draws. So the
first frame without a HUD still got the overlay, with the previous frame's
ring masks, over an empty screen. It lasts one frame; the screenshot's long
frame is what kept landing on it.

## Fix

`x2_gameplay_control_on_screen(frame)` answers whether the HUD drew in the
frame being presented, and the overlay is rendered only then
(`touch_runtime_overlay_on_screen`). Input routing keeps the
current-or-last-frame rule from 0210.

Proven 2026-11-12, run 53: the host's frame after the client left shows the
world with neither the retail HUD nor any touch control, and the drop popup
follows.

The ring masks still describe the HUD of the frame before. A portrait that
hides keeps its ring for one frame.
