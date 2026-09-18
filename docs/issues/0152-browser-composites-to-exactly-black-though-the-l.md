---
id: 152
title: Browser composites to exactly black though the logical scene has content; --vk-selftest hangs at the first fence on WebGPU
status: investigating
symptom: browser run presents pure black (composed max 0) while the logical D3D scene it composites has content (max 191); --vk-selftest never prints a result in the browser though it passes on native
tags: web,browser,wasm,gpu,present,webgpu,sdl,readback
created: 2026-09-15
updated: 2026-09-18
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

### Update (2026-09-18): black canvas CONFIRMED to still reproduce in real gameplay; boot blackout ruled out; a third independent instrument rules out candidate (b)

Re-ran the Dead Zone route (`--test-deadzone`, `--set present_luma=20`) in-browser via WebLua now that the logging fix means console output is no longer withheld. The black canvas is real and reproduces exactly as before:

```
[LUMA] present 21: composed mean 0.0 max 0 nonblack 0.0% | scene read mean 0.1 max 191 nonblack 0.1% (frame 1280x720, scene 1280x720, 5 draw(s) so far)
[LUMA] present 91: composed mean 0.0 max 0 nonblack 0.0% | scene read mean 0.0 max 73 nonblack 0.0% (frame 1280x720, scene 1280x720, 9 draw(s) so far)
```

Presents are NOT stuck -- crossings/blocks-translated/presents all climb steadily in the periodic `[HB]` heartbeat (e.g. presents 60->64 over a 5s window at ~226s wall time), so this is not a frozen boot; the game genuinely runs and genuinely presents frame after frame, all of them black on screen.

Two things this session ruled out, mechanically rather than by inspection:

* **Boot blackout is not armed.** `x2_boot_blackout_arm()` only fires from `boot_to_host_mode()` in `src/native/startup.c`, gated on `x2_settings_store()->boot_mode != X2_BOOT_NORMAL` -- and the default is `X2_BOOT_NORMAL`, so for a plain `--test-deadzone` run (which uses `X2_BOOT_MAP`, a different code path) it never arms. Confirmed empirically: grepping the full console capture for "blackout" found zero lines, including no "boot blackout: armed" line. Ruled out as a candidate for this run's black frames, not merely deemed unlikely.
* **Candidate (b) from the 2026-09-15 note ("the capture-owner download mis-lays-out / returns zero ONLY on the WebGPU browser backend") is refuted by a THIRD, independent instrument.** Sampled the live `<canvas>` element directly from the page -- `createImageBitmap(canvas)` into an `OffscreenCanvas`, then `getImageData` -- which reads back through the browser's own compositor, entirely outside any of this title's SDL/GPU capture code. Result: `{w:1280, h:720, mean:0, max:0, nonblackPct:0}`. The pixels the browser is actually compositing to the screen are black, not merely what one CPU readback path reports. A companion screenshot is saved at `scratch/web/gameplay-black-2026-09-18.png`.

So the defect is a genuine composite-content bug, not a stale/broken readback and not a presentation policy withholding frames deliberately. Traced one more layer: `gpu_frame_end()` in `src/gpu/gpu_device.c` only routes the composite through the retained capture texture (`gpu_capture_frame_target`) on the specific frame `present_luma` has requested via `gpu_capture_request()` (`presents % every == 0`); every other frame's `final_output` is the real acquired swapchain texture (`g_output`) directly, and `gpu_present_composite()` blits `g_scene` straight into it. Both paths -- composite into the swapchain texture on an ordinary frame, and composite into the retained offscreen `g_capture_texture` on a `present_luma`-requested frame -- are confirmed black (the canvas sample above was NOT necessarily taken on a capture-requested frame, so the ordinary swapchain-direct path is implicated too, not just the capture path). `gpu_present_composite()` itself is character-for-character the same function `--vk-selftest` exercises and passes; `x2_aspect_fit(1280,720,1280,720,...)` is a 1:1 non-degenerate mapping (ruled out via the log's own `frame 1280x720, scene 1280x720` line, matching the 2026-09-15 note's reasoning).

What's different between the passing selftest and the failing real loop: the selftest's `g_scene` is written and fenced in one isolated, one-shot offscreen sequence before a *separate* composite submission reads it; the real loop draws into `g_scene` and composites out of it *within the same continuously-reused command buffer*, frame after frame, across an acquired swapchain image whose lifecycle (`SDL_WaitAndAcquireGPUSwapchainTexture` each frame) the selftest's second phase touches only once and never composites through. Next step (still cross-repo, still browser-only, per the original ask): trace whether `SDL_BlitGPUTexture` is actually being encoded and executed on the WebGPU backend across a *sustained* multi-frame loop with real swapchain acquisition each frame -- not a single offscreen shot -- since that sustained/swapchain-cycling case is the one variable the passing selftest does not cover. A fix must not be guessed without first showing whether the blit command is missing from the encoded command buffer, silently failing during encoding, or executing against the wrong bound texture on that specific WebGPU code path.

### Update (2026-09-18, later): the "sustained swapchain compositing" hypothesis is REFUTED -- the SDL fork is not at fault; the defect is specific to this title's own runtime state

Built the discriminator the previous update called for: `testgpu_webgpu_sustained.c` (new, in the scratch fork at `~/repo/shared/web-port/scratch/sdl-fork/test/`, not upstreamed -- it is diagnostic-only) claims a real SDL window, acquires the real swapchain via `SDL_WaitAndAcquireGPUSwapchainTexture` every frame for 40 frames, and on frame 30 blits a known non-black scene texture directly into that frame's freshly-acquired swapchain texture with the exact same `SDL_BlitGPUTexture` call shape `gpu_present_composite()` uses (`LOADOP_CLEAR`, linear filter, aspect blit). Sampling the live `<canvas>` afterward the same way the 2026-09-18 update above did for the real title (`createImageBitmap`+`getImageData`, outside any GPU-capture code): `{w:300, h:150, mean:127.5, max:255, nonblackPct:100}`. Non-black. The exact "sustained, swapchain-reacquired-every-frame compositing" shape this update set out to test WORKS CORRECTLY on the pinned SDL WebGPU backend in isolation.

(One test-authoring dead end on the way, recorded so it isn't repeated: an attempted extra "retain a copy of the swapchain frame" step tried to use the just-acquired swapchain texture as a blit SOURCE, and separately tried a direct `SDL_DownloadFromGPUTexture` copy from it. Both were rejected by WebGPU's own validation -- `usage (CopyDst|RenderAttachment) doesn't include TextureBinding` for the sampling attempt, and `...doesn't include CopySrc` for the direct copy attempt, which also invalidated that entire command buffer and silently dropped the valid composite blit encoded earlier in it. This is a genuine, useful fact about the backend -- a browser WebGPU swapchain texture here is write-only (`CopyDst|RenderAttachment`) and cannot be read back at all, which is exactly why the title's own `gpu_capture_frame_record` refuses when `rendered == output` and always downloads from a separately-created offscreen retained texture instead. It is not the cause of the black canvas; it only broke this test's own bonus verification step, and mixing it in produces a false "0 nonblack" reading that looks like this bug but isn't -- confirmed by removing it and reading the canvas directly instead.)

This rules out "sustained swapchain compositing is broken on the browser WebGPU backend" as the root cause. The composite/blit-into-swapchain mechanism is proven correct in isolation for exactly the shape (real window, real per-frame swapchain acquisition, many frames, direct blit-to-swapchain) that distinguishes the real failing case from the passing offscreen selftest. The defect must therefore be in something specific to the real title's runtime state or code path that this minimal repro does not share -- candidates not yet tested: the depth-binding texture/render pass xmen2 creates alongside the scene target, the game's own draws into `g_scene` running on a genuinely different thread/timing under `PROXY_TO_PTHREAD` such that the composite blit within a frame's command buffer races against or precedes the scene's own render pass completing, or `present_luma`'s/`gpu_capture`'s extra command-buffer submissions interleaving with the main loop's `g_cmd` in a way that serializes incorrectly only in-browser. Next step: instrument the REAL title's `gpu_present_composite()` call (not a synthetic proxy) with the same EM_JS `GPUQueue.submit`/error-capture tracing used in the scratch fork's probes, to see directly what parameters and texture state the blit actually executes with on a black frame.

### Update (2026-09-18, later still): real-title composite call traced -- clean parameters, no error; an alpha/blend theory was checked and refuted by evidence already in hand

Added a temporary trace (`src/gpu/gpu_present.c`, reverted after use, not committed) logging `g_scene`/`output` pointers, sizes, the aspect-fit destination rect, and `SDL_GetError()` immediately after `SDL_BlitGPUTexture` in the REAL title's `gpu_present_composite()`, for its first 6 calls. In-browser on a `--test-deadzone` run:

```
gpu present: TRACE composite #2 scene=0x3bdfaf8(1280x720) output=0x2b934e08(1280x720) dest=0,0 1280x720 SDL_GetError=""
gpu present: TRACE composite #5 scene=0x3bdfaf8(1280x720) output=0x2b977f90(1280x720) dest=0,0 1280x720 SDL_GetError=""
```

Both textures are valid non-null pointers, sizes agree (1280x720 in both, matching the passing selftest and the minimal sustained repro), the destination rect is the full non-degenerate frame (0,0 1280x720, a 1:1 aspect-fit as expected), `output` visibly cycles across calls (different swapchain images, as it should), and `SDL_GetError()` is empty every time -- no error at blit-encode time. No `WebGPU uncaptured error` browser-console line accompanied any of these (that class of error, confirmed reproducible with a deliberately-invalid call in the scratch fork's sustained-composite test, always logs to the console independent of `SDL_GetError()`; its absence here means WebGPU's own validation accepted every one of these blits).

While reading the WebGPU backend's blit pipeline setup (`SDL_GPU_FetchBlitPipeline` in `SDL_gpu.c`, `WEBGPU_INTERNAL_CreateGraphicsPipeline` in `SDL_gpu_webgpu.c`) to look for a destination-format-dependent difference between the real title and the minimal repro, found that the blit pipeline's `SDL_GPUColorTargetDescription.blend_state` is entirely zero-initialized and `WebGPUTextureFormatIsBlendable()` defaults to `true` for `B8G8R8A8_UNORM` -- so blending IS enabled on the blit pipeline for this format, with all-zero blend factors/op, which looked at first like a real candidate for "every blit composites as if multiplied by a zero factor, regardless of source content." Did NOT stop at reasoning about this: it is directly refuted by evidence already collected in this same investigation -- the minimal `testgpu_webgpu_sustained.c` repro above uses the IDENTICAL destination format (`B8G8R8A8_UNORM`), the IDENTICAL blit call shape, and produced non-black, fully-opaque, correct-looking output (`mean:127.5, max:255, nonblackPct:100`). A zero-blend-factor theory would have made that repro black too. Recorded here specifically so this dead end isn't walked again.

Net position: the composite blit is encoded with sane, correct parameters, against valid textures, with no WebGPU-level or SDL-level error, on the exact frames that `present_luma` later reports as fully black. The defect is not in blit encoding, not in texture identity/size, not in blend state (checked and ruled out), and not in the destination format. What remains unverified is the ACTUAL CONTENT of `g_scene` at the precise moment this blit executes -- `present_luma`'s own scene readback is a separate, later GPU submission that runs immediately after the frame (not concurrently with the composite), so a genuine stale/wrong-frame content race there has not been ruled out, and neither has interference from the depth-binding render pass or from `gpu_capture`'s extra submissions specifically under `PROXY_TO_PTHREAD`. This needs a same-command-buffer content check (download `g_scene` in the identical command buffer as the composite blit, before any other frame's draws can touch it) to close -- a more invasive probe than this session had budget to build.

### Update (2026-09-18, final this session): the "scene" reading itself is now the more suspect half of the pair -- reopens whether this is a distinct bug at all, versus the already-known framerate problem

Two more mechanisms were checked and ruled out cheaply: caching/reuse of the retained capture texture across a long session (`testgpu_webgpu_retained_reuse.c`, new scratch-fork probe -- 200 sustained frames, 40 separate composite+download cycles into the SAME cached texture, exactly mirroring `gpu_capture_frame_target`'s caching: every one of the 40 came back 100% non-black); and `x2_ui_render` running after the composite and silently clearing the target (it early-returns as a no-op unless the settings or touch overlay is visible, which it isn't on this route -- one read of `src/ui/rmlui_ui.cpp`, no rebuild needed).

Then let the real Dead Zone run continue far longer than any previous sample in this issue -- past 5 minutes of wall time, well over 1200 real draws and 220 real presents (heartbeat: `gpu draws 2099 (+73) refused 0 (+0)`, `combiner args: 2099 default, 0 other`, `0 of 2099 draw(s) wanted a texture stage beyond 0`). The `[LUMA]` line was sampled five times across this run, at presents 21, 91, 141, 201 and 221:

```
scene read mean 0.1 max 191 nonblack 0.1%   -- every single time, byte-identical
```

`composed` stayed at exactly `mean 0.0 max 0` throughout, as before. But the "scene" number being bit-for-bit IDENTICAL across five samples spanning 5+ minutes of real engine execution and roughly 2000 real draw calls is itself now the more suspicious reading, not the corroborating one. `gpu_readback_texture_rgba()` (`src/gpu/gpu_readback.c`) was read in full: it is a genuine fresh download every call (new transfer buffer, new fence, new map, no caching), so this isn't a stale-buffer bug in the readback code itself. The far more likely explanation is that `g_scene` really does contain the same near-empty content every time because the ~2000 draws counted so far are NOT yet drawing visible level geometry -- every one of them uses texture stage 0 only (`0 of 2099 draw(s) wanted a texture stage beyond 0`, `2099 default combiner`, i.e. flat/simple draws, not textured terrain or character models), consistent with a loading screen, splash element, or very early boot state rather than the Dead Zone level itself.

This reopens a more basic possibility that the previous updates did not check: given the framerate is independently measured elsewhere as ~666-671 ms/frame ("not playable"), and this session's own heartbeat shows the SAME order of magnitude (roughly 220 presents in over 5 minutes), it is plausible the game genuinely has not finished loading into a real, visually rendered frame within any window this issue has tested. If so, "the composed frame is black" and "the game hasn't drawn a real level yet" may be THE SAME underlying problem (severe under-performance), not two separate defects -- and the composite/capture MECHANISM this issue spent most of its effort proving correct may never have been the problem at all.

**Not yet done, and the clear next step:** run the Dead Zone route for substantially longer (tens of minutes, or until draws-per-scene and texture-stage usage visibly changes character -- e.g. draws start using texture stage 1, or `scene`'s mean/max actually moves), and separately confirm whether the map load genuinely completed (the `startFirstMission`/`X2_BOOT_MAP` log lines already show it did dispatch the map) versus the level's actual geometry never having rendered a first real frame. This determines whether issue #152 is a distinct rendering defect or is subsumed by the framerate problem entirely -- and it should be resolved before any further composite/capture-mechanism investigation, since that mechanism is now thoroughly proven correct and is very unlikely to be the actual cause.

Tried `--set unbounded=1` (`X2_UNBOUNDED`, the clock-pacing cvar named in the `[HB]` heartbeat's `clock:` line) to reach that longer observation faster: it does make the guest run genuinely faster (crossings climbed ~47,700 in 5s wall time, versus a much lower rate paced), but it produced a NEW, different failure on this exact route -- `scenes`/`draws`/`presents` froze at 1 for the entire ~40s tried, while crossings kept climbing, i.e. the guest is burning real CPU in a busy loop that never reaches a second `Present`. `clock: UNBOUNDED. 0 of 0 idle wait(s) skipped` shows it isn't even reaching its normal idle-wait pacing point -- this isn't the documented wait-skip mechanism doing anything harmful, it's a distinct stall. Not investigated further (orthogonal to this issue; `--unbounded` is diagnostic-only and not part of any shipping path); reverted to the normal paced run for the long observation. Recorded here only so it isn't rediscovered as a surprise if someone reaches for `--unbounded` to speed up this or a future browser diagnostic.
