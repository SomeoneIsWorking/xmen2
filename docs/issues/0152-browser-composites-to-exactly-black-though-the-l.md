---
id: 152
title: Browser composites to exactly black though the logical scene has content; --vk-selftest hangs at the first fence on WebGPU
status: investigating
symptom: browser run presents pure black (composed max 0) while the logical D3D scene it composites has content (max 191); --vk-selftest never prints a result in the browser though it passes on native
tags: web,browser,wasm,gpu,present,webgpu,sdl,readback
created: 2026-09-15
updated: 2026-09-15
---

Affected state: S021 (web WASM product), contract W2.

ROOT CAUSE OF "black canvas" LOCALIZED, and it is browser-only, in the
composite/present fence path -- NOT the game->GPU draw path, not the wait
convoy (#149), and not a page-screenshot artifact.

Evidence, all from the new trusted in-engine instrument (`present_luma`, which
reads back the presented frame AND the logical D3D scene it composites, and was
unit-tested to show BOTH answers):

* Native windowed control (Xvfb, real swapchain, `x2native --d3d8
  X2_PRESENT_LUMA=40`, scratch/web/native-window-luma2.log): the probe reports
  `scene read mean 34.2 ... composed mean 34.2` -- identical and non-black --
  and `--vk-selftest` prints `gpu present selftest: PASSED`.
* Browser run of the packaged build on the Dead Zone route
  (scratch/web/iter-luma3.log): `composed mean 0.0 max 0 nonblack 0.0% | scene
  read mean 0.1 max 191 nonblack 0.1%`. At 1280x720 scene into 1280x720 output
  the aspect-fit is a 1:1 copy, so a scene with a max-191 pixel cannot
  legitimately composite to exactly 0 -- the composite is dropping content it
  is handed.
* Browser `--vk-selftest` (no game install needed): prints
  `=== gpu present selftest: logical scene aspect-fit pixels ===` then NOTHING
  -- no PASS/FAIL, no further heartbeat. The first `submit_and_wait`/readback
  of the composite never completes on the WebGPU browser backend, where the
  identical battery passes on native Vulkan and the readback path returns real
  numbers in the in-game probe.

So the defect is in the maintained SDL-WebGPU fork's fence-wait / blit-to-
retained-target completion on the browser (the `web-port`/SDL dependency), not
in this title. The in-game readback does return numbers, but the aspect-fit
composite yields exactly black, and the same composite run under --vk-selftest
hangs before reporting -- both point at the same WebGPU blit/fence boundary.

Next step (cross-repo, browser-only): reproduce the fork's composite/capture
fence in isolation on the WebGPU backend, name whether the fence ever fires or
the sampled scene view is the wrong texture, and fix it in the fork. A
browser-side trace or the fork's own test suite -- not this title's native
gates -- is the discriminator. Until then a WASM run is functionally playable
through JIT + input + audio but shows nothing, so browser rendering stays
`partial` and gameplay is unqualified.
