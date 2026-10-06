---
id: 175
title: Firefox never answers the storage permission prompt, so the setup page never opens
status: resolved
symptom: the deployed page is a black screen in Zen/Firefox with WebGPU enabled; it stays on its first status line with every button disabled
state_items: S021
tags: web,browser,firefox,zen,storage,opfs,setup
created: 2026-09-19
updated: 2026-09-19
---

# 0175 — Firefox never answers the storage permission prompt, so the setup page never opens

State item: S021 (web product)
Status: FIXED in `shared/web-port` c1bff1c and pinned here.

## What a player saw

Reported against `https://someoneisworking.github.io/xmen2/` in Zen 1.22.2b
(Firefox 156) with WebGPU enabled: "the game is just black screen". Every
browser measurement this port had ever made was headless Chrome, so the
Firefox family was entirely unmeasured.

## What it actually was

Reproduced in headless Zen over Marionette (`tools/marionette_client.py`).
The page was not black because rendering failed; it never got as far as
rendering. It sat on the initial status line of `index.html`, "Preparing
browser storage…", with `#play`, `#test-play` and `#archive` all disabled.

The measured cause, from the page itself:

| call | Chrome | Zen 1.22.2b |
|---|---|---|
| `navigator.storage.getDirectory()` | resolves | resolves, 0 ms |
| `navigator.storage.persisted()` | resolves | resolves, `false` |
| `navigator.storage.persist()` | resolves | **never settles** (15 s cap hit) |

`persist()` is a permission request. Firefox is entitled to hold it until a
player answers a prompt, and in an ordinary window that prompt is easy to
never see. `persistentStorage()` awaited it, so the whole application was held
behind an unanswered question about a nicety — whether the browser promises not
to reclaim storage under pressure — instead of proceeding to the thing the
player came for.

The first load also has a legitimate reload: GitHub Pages cannot set COOP/COEP,
so the service worker supplies them and the page reloads once to pick up cross
origin isolation. That part works in Zen: `crossOriginIsolated` was `true` and
`SharedArrayBuffer` was a function on the second load. It was not the defect.

## The fix

`shared/web-port` `platforms/web/storage.mjs` now takes the OPFS root, reports
`navigator.storage.persisted()` — the state that is true now — and hands the
caller the pending request as `granted` so the note can improve if the answer
ever arrives. `web/app.mjs` folds a late grant into `#storage-note` and never
waits on it.

`shared/web-port` `tests/storage_verify.mjs` runs the packaged module against a
Storage Manager that never settles, one that grants late, one that rejects, one
already persistent, and one with no OPFS at all. It was run against the
superseded implementation and fails it by name
("persistentStorage() with an unanswered prompt did not answer within 5s").

## Verified

Headless Zen against the rendered release served WITHOUT isolation headers, so
the service-worker path is the one exercised, as on Pages:
`crossOriginIsolated: true`, status "Choose your game ZIP or play the
installation saved on this device.", `#archive` enabled, on three consecutive
loads.
