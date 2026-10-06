---
id: 186
title: keeping samplers across a pipeline change blacked out the browser canvas
status: resolved
symptom: browser canvas exactly black again (mean 0, max 0) while scenes, draws and presents advance; the worker spends about 1.7% writing "WebGPU uncaptured error!" to stderr, which no captured log shows
state_items: S021
tags: web,browser,webgpu,sdl,rendering,regression,logging
created: 2026-09-24
updated: 2026-09-24
---

# 0186 — keeping samplers across a pipeline change blacked out the browser canvas

## Cause

`c804823` made a draw skip `SDL_BindGPUFragmentSamplers` when the pass already
holds the same samplers (`gpu_pass_binds`). That is correct SDL_GPU usage, and
Vulkan, D3D12 and Metal keep a pass's bindings across a pipeline bind. SDL's
WebGPU backend did not: `WEBGPU_BindGraphicsPipeline` cleared every texture and
sampler binding. The next draw therefore built its fragment bind group from an
all-zero cache key.

That cache was keyed by the bound resources only, without the layout. So the
draw was handed a group made earlier for another pipeline's layout. Dawn
rejected it, and with it the whole command buffer:

```
The bind group layout [BindGroupLayout "Sampler Storage Bind Group Layout (Render)"]
of pipeline layout [PipelineLayout (unlabeled)] does not match layout [...] of
bind group [BindGroup (unlabeled)] set at group index 2.
[Invalid CommandBuffer] is invalid.
```

It is the same effect #152 had: every frame's composite was discarded.

## Why nobody saw the error

SDL's default log output writes straight to stderr. In the browser that is the
worker's own `console.error`, not the port's batched page console, so the
WebLua capture never held a word of it. The only trace was a profile:
`_emscripten_err` and the UTF-8 decoder behind it were about 1.7% of the guest
worker, reached from `WEBGPU_INTERNAL_UncapturedErrorCallback`.

`src/native/sdl_host_setup.c` now routes SDL's log through the port's logger,
on channel `sdl`.

## Fix

`SomeoneIsWorking/SDL`, pinned through `shared/web-port` `f9202b0`:

- `22d9d54af`: a cached bind group is keyed by the layout it was made for.
- `8d2884c3f`: a pipeline bind keeps the pass's bindings and marks the groups
  outdated. Bindings are cleared when a render pass begins. A group holds what
  the pipeline's shaders declare, and the pipeline header now records those
  counts, as the other backends do.
- `a42df2278`: a draw sets only the groups that changed.

## Evidence

The canvas was sampled through the browser's compositor, the #152 instrument:
`createImageBitmap(canvas)` into an `OffscreenCanvas`. Each run was a fresh
`#test-play` load in the Dead Zone route, with the served wasm size checked.

| SDL | canvas mean / max / non-black | uncaptured errors |
|---|---|---|
| `d4b323257` (previous pin) | 0.0 / 0 / 0.0% | 121 |
| `a42df2278` via `--sdl-source` | 32.4 / 255 / 90.9% | 0 |
| `a42df2278` via the pin (10,114,411-byte wasm) | 32.5 / 255 / 91.0% | 0 |

The canvas showed the Magneto dialog over the Dead Zone jetty. WebLua's own
`screenshot` returns black for a headless WebGPU canvas either way, so it is not
evidence here.
