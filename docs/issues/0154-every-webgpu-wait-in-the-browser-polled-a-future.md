# 0154 — every WebGPU wait in the browser polled a future that could not resolve

- **State items:** S021
- **Status:** fixed in the SDL fork; browser frames now advance, frame time is still bad
- **Supersedes the reading in:** #149 (the wait was not the guest lock alone), #153's
  "What this did NOT fix"

## What it was

SDL's WebGPU backend waited by polling. Fence waits, the swapchain acquire
loop, and the device-destroy drain all sat in a loop calling

```c
wgpuInstanceWaitAny(renderer->instance, 1, &fence->future, 0);
```

and asking again. A zero timeout is the one form of that call which never
reaches JavaScript: `emdawnwebgpu` walks its event map under a mutex and
returns. Only a **non-zero** timeout takes the Asyncify path that unwinds the
stack, lets the browser run, and resumes once the promise settles —
`webgpu.cpp` says so in as many words: *"To handle timeouts, use Asyncify and
proxy back into JS."*

So the poll could not observe a completion. A future's promise settles when the
thread returns to its event loop, and a thread sitting in the poll loop is by
definition not doing that. Every one of those waits could only end by luck —
some unrelated part of the frame happening to yield first — or not at all.

The backend's own comment beside the call already knew:

> Despite its name, WaitAny isn't actually blocking unless the TimedWaitAny
> instance feature is enabled, and you give a value to the timeoutNS argument.

The instance was created with the default descriptor, so the feature was off
and the only legal timeout was 0.

## How it presented

Three symptoms that looked like three defects:

1. **`--vk-selftest` hung.** The worker logged `gpu: swapchain claimed on
   window`, entered `WEBGPU_WaitAndAcquireSwapchainTexture`, and never came
   out. That loop also discarded the `bool` it collected, so an acquire that
   *failed* was retried forever with the error thrown away. Measured: 117% of a
   core for 17 hours. The 15 sibling workers produced 575,051 samples between
   them in five seconds; the stuck one could not be sampled once, because a
   thread in `Atomics.wait` cannot service the inspector.
2. **Gameplay wedged at 10 presents** with draws frozen at 121 and
   `emscripten_futex_wait` at 30.27% of the busy worker.
3. **Frame wall time of 600-3,700 ms** with host draw at 2.8 ms and host upload
   at 0.3 ms — the renderer's own work was never the cost.

## The fix

SDL fork `89951aff5`, pinned through web-port `4407360`:

- the instance is created with `WGPUInstanceFeatureName_TimedWaitAny` and a
  `timedWaitAnyMaxCount`, and reports it if that fails rather than silently
  degrading;
- the fence wait yields with a real timed `wgpuInstanceWaitAny`, which returns
  the moment the future completes;
- the acquire loop waits on the **oldest submitted** fence, which is the one
  whose completion frees a frame-in-flight slot, and propagates an acquire
  failure instead of looping on it;
- every remaining wait is bounded by one deadline in
  `SDL_gpu_webgpu_wait.h` and says what it was waiting for when it expires,
  so a wait that cannot succeed fails loudly instead of silently.

## Measured, same route, same page command line

| | before | after |
|---|---|---|
| presents | wedged at 10 | 252 in under 3 min |
| draws | frozen at 121 | 44,746 (+2,114 per 5 s) |
| `emscripten_futex_wait` | 30.27% of the busy worker | absent from the profile |
| `--vk-selftest` | hung forever after "swapchain claimed" | runs the battery to a verdict |

## What this did NOT fix

Frame time is ~600 ms — about 1.4 fps. That is no longer a wait: the profile is
guest execution and memory access. Device teardown still costs about 10 s per
device in the browser, which is now the bounded timeout expiring rather than a
hang, and it points at queued destroys whose reference counts never fall. And
the battery now reaches a real renderer defect it could never get to before:

```
gpu multistage selftest: FAILED -- mip control/mipped centres are
0xff00ff00/0xff00ff00, expected red/green.
```

Both centres came back green where one must be red, so the mip control sample
is wrong in the browser. That is a lead for #152, not a separate hunt.
