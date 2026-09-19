---
id: 170
title: a finger on the browser's game canvas scrolls the page
status: resolved
symptom: touch on the canvas was handled by the page, so a contact scrolled instead of reaching the overlay
state_items: S020,S021
tags: web,browser,touch,input,canvas
created: 2026-09-19
updated: 2026-09-19
---

# 0170 — a finger on the browser's game canvas scrolls the page

State items: S020 (platform-neutral touch play), S021 (web product)
Status: the gesture defect is fixed and measured; the safe area is written at
its owner and not yet proven.

## Symptom

The port's on-screen pad is platform-neutral by construction and unit-verified
(S020), and the web target was named as its third consumer. It is not one. In a
mobile browser the player's thumb never reaches the game: the browser claims the
gesture first.

## Measured

`scratch/web/touch/probe.py` drives a real emulated touch device over CDP
against the shipping page and reports each browser gesture separately. On the
package built at `295fba4`, 390x844, one twelve-step drag up the middle of the
canvas — the shape of a thumb on the virtual stick:

| observation | before | after |
|---|---|---|
| document scrolled under the finger | **551 px** | 0 px |
| canvas `touch-action` | `auto` | `none` |
| canvas `user-select` | `auto` | `none` |
| touchstart events seen | 1 | 1 |

The drag was not lost, mis-ordered or swallowed: the page received it and spent
it on its own scroll. A long press would additionally have opened the context
menu, and a second finger would have zoomed the page rather than reaching the
second control.

The probe is built to print the other answer. It pads the document to 400vh
first, so a page that simply cannot scroll would not read as a pass, and it
refuses outright if no touchstart reached the document rather than reporting a
tidy row of "no gesture".

## Cause and fix

The page had never said the canvas gestures belong to the application. That is
page mechanics, identical for every consumer, so it is owned once in
`shared/web-port` at `platforms/web/canvas.mjs` (`claimCanvasGestures`), a
reserved release resource alongside `storage.mjs` and `isolation.mjs`. It sets
`touch-action: none`, stops selection, the callout, the tap highlight and the
native bitmap drag, and turns off document overscroll so an edge swipe cannot
fire pull-to-refresh. `web/app.mjs` claims the canvas before the game can
start, so no early contact is spent teaching the page whose gesture it is.

`web/style.css` also sized the canvas with `100vh`, which is the viewport with
the mobile address bar retracted — the bottom of the canvas, where the stick and
the action cluster sit, is off screen until the page scrolls, which the claim
now prevents. It is `100dvh` with the `100vh` declaration kept ahead of it as
the fallback.

## Still open: the safe area

`SDL_GetWindowSafeArea` returns the whole window on Emscripten, because that
backend never called `SDL_SetWindowSafeAreaInsets`. A control placed against the
edge therefore sits under a notch or the home indicator, and the title cannot
tell that case from a display with nothing in the way. The title code is right
and must not change: the fix belongs to the backend that owns the answer
everywhere else.

SDL fork `70f8057` reads `env(safe-area-inset-*)`, intersects the viewport's
safe rectangle with the canvas box, and publishes the result at window creation
and on every resize. It compiles for wasm32 and is **not pinned into web-port or
this port**, because it is not yet evidence: Chrome's CDP here has no
`Emulation.setSafeAreaInsetsOverride`, so a notch cannot be emulated, and a run
in which every inset is zero cannot tell a correct reader from one that returns
zero. Pinning it needs either a browser that can emulate insets or a device run.

## Not done

Claiming the gestures makes a contact reach the application. It does not by
itself prove the pad is playable in a browser: that needs a run in which a
touch on a drawn control moves the game, which is gameplay evidence and remains
part of S020's open gap.
