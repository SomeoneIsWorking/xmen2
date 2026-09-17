---
id: 152
title: Browser composites to exactly black though the logical scene has content; --vk-selftest hangs at the first fence on WebGPU
status: investigating
symptom: browser run presents pure black (composed max 0) while the logical D3D scene it composites has content (max 191); --vk-selftest never prints a result in the browser though it passes on native
tags: web,browser,wasm,gpu,present,webgpu,sdl,readback
created: 2026-09-15
updated: 2026-09-17
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

### Note (2026-09-15)
PRECISION (do not overclaim): the two in-engine readback paths are different code. `scene` uses the new `gpu_readback_texture_rgba`; `composed` uses the capture owner's `gpu_capture_frame_record/complete`. On native BOTH are exercised by `--vk-selftest` (which PASSES, asserting exact corner colours through the capture path), so the capture download is correct there. The browser disagreement (composed 0 vs scene max-191) therefore has two candidate causes that this run cannot yet separate: (a) the WebGPU composite genuinely black-outs the aspect-fit blit, or (b) the capture-owner download mis-lays-out / returns zero ONLY on the WebGPU browser backend, while my separate scene-readback happens to work. The page screenshot is not a clean third instrument here (it can capture before the canvas paints, and the fork's blue-canvas test proves the canvas itself CAN show colour).

What IS established: browser rendering is unqualified and native is fully verified non-black; the wait convoy (#149) and arg misrouting (#151) are fixed and are NOT this black. What must come next, in a browser-only fork trace (shared SDL-WebGPU fork), is a single instrument that photographs the actual swapchain the window shows -- not two different CPU readbacks -- so the black is attributed to a named stage (composite blit vs capture download vs present) before any fix is written. A fix must not be guessed between (a) and (b).

### Correction (2026-09-17): the "--vk-selftest hangs" premise was false; it is not a WebGPU defect

The browser-only fork trace this issue called for was run, and it disproved the issue's own headline claim. `--vk-selftest` does not hang in the browser: it prints `gpu present selftest: PASSED -- wide and tall scenes used the production compositor and retained capture, with black bars, preserved corner colours and a bounded allocation` on the actual pinned `SomeoneIsWorking/SDL` WebGPU backend, with no patch to that fork. What looked like a hang was `src/web/browser_log.cpp`'s `BatchConsoleSink`: it withholds non-error lines until 64 lines / 32KB accumulate, or an ERROR line arrives, or `main()` returns and calls `flush_browser_log()` -- the only unconditional flush, reached exactly once, at the very end of `main()`. `--vk-selftest` logs one ERROR line early (the expected missing-settings-file message, which forced an immediate partial flush and was the only visible output) and then only a handful of INFO lines -- never enough to cross the batch threshold, and it does not return from `main()` in the observed window. The selftest's PASS/FAIL line was sitting in the unflushed batch the whole time, not lost, and nothing was actually stuck on a fence.

Fixed in `src/web/browser_log.cpp` with a bounded periodic flush independent of `main()`'s own progress (`SDL_AddTimer(250, ...)` calling `flush_browser_log()` every 250ms). Verified twice in-browser via WebLua against the packaged build: once with an instrumented scratch SDL fork carrying extra WebGPU trace/device-lost logging (diagnostic only, not part of the fix, not upstreamed -- lives at `~/repo/shared/web-port/scratch/sdl-fork`, outside any tracked xmen2 or shared-repo path), and again with a clean rebuild against the actual pinned, non-instrumented SDL fork dependency (`tools/build_web.py` with no `--sdl-source` override). Both times `--vk-selftest` produced `PASSED` within seconds of the click instead of appearing to hang indefinitely.

What this settles: the composite blit, retained-capture download, and multi-submission fence-wait sequence (the exact code `gpu_present_composite`/`gpu_capture_frame_record` use) are all correct on the browser's WebGPU backend. Candidate (a) from the note above -- "the WebGPU composite genuinely black-outs the aspect-fit blit" -- is refuted for the synthetic selftest case.

What remains OPEN: the original gameplay symptom (`present_luma` reporting `composed mean 0.0 max 0` on the Dead Zone route while `scene mean 0.1 max 191`) has not been re-run since this logging fix landed. The selftest exercises an offscreen synthetic composite, not the real windowed swapchain claim/acquire/present cycle under actual gameplay load; it does not by itself prove the real run is non-black. Candidate (b) (a capture-owner download defect specific to that different code path/context) is also not yet ruled back in or out for the real gameplay case. Next step: re-run the Dead Zone `present_luma` probe now that console output cannot be silently withheld, and either close this issue or narrow it to a `present_luma`-specific stage.
