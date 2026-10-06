---
id: 160
title: a browser with no GPU kills the guest worker instead of refusing
status: resolved
symptom: in a Chrome started without GPU support the guest boots, initialises D3D8, and the worker is killed rather than refused with a message
state_items: S021
tags: web,browser,wasm,gpu,refusal
created: 2026-09-19
updated: 2026-09-24
---

# 0160 — a browser with no GPU kills the guest worker instead of refusing

- **State items:** S021
- **Status:** resolved -- three causes, each fixed where it lives

## What happens

Run the browser product in a Chrome started without GPU support. The guest
boots normally, maps every module, initialises D3D8 — and then:

```
[x2:error]  *** Display failed! Unable to initialise graphic display.
            Resolution and FSAA have been reverted to default.
error   Pthread 0x01d576b8 sent an error!
        http://127.0.0.1:8142/x2native.js:1: Uncaught ReferenceError: document is not defined
error   Uncaught RuntimeError: null function or function signature mismatch
error   Uncaught ReferenceError: document is not defined
```

The guest then never executes again. The heartbeat reports `crossings unchanged
at 25384` every five seconds for as long as the run is left going, with `MAIN
tid 999 running guest code` and the JIT snapshot permanently pending — the
shape of #158, from a cause that is visible here and was not there.

## Why it matters

`document` does not exist on a pthread worker. Something on the display
failure path reaches for the DOM from the worker that runs the guest, throws,
and takes the worker with it. A player whose browser has no hardware
acceleration — a remote desktop, a VM, a laptop on battery with the GPU
blocklisted, any of the machines Chrome disables acceleration on — meets a
frozen page with nothing said.

Two separate defects, and the second is the one that matters:

1. The product does not check for the renderer it needs before the guest asks
   for a device. If WebGL is a requirement, saying so at startup is a one-line
   refusal.
2. The failure path is not worker-safe. A DOM access from a guest worker is a
   crash, not a fallback, and the "reverted to default" message says the code
   believed it was recovering.

This is the reverse of the rule the rest of this port keeps: refuse by name,
loudly, rather than continue in an unknown state. Here it neither refused nor
continued.

## How it was found

By accident, and that is worth recording: a WebLua session was restarted
without `--gpu` while measuring something else. Every browser measurement in
this project so far has run on a GPU-enabled headless Chrome, so the
no-accelerator path had never been exercised.

## What would falsify the diagnosis

If the same run wedges identically in a GPU-enabled browser, `document` is not
the cause and the wedge is #158 arriving by another route. Measured here: the
same build, the same route, the same page, with `--gpu` — the run proceeds past
`CreateDevice` and presents frames.

## Cause and correction

There were three defects, not one, and the worker's own stack was needed to
separate them. The recorder could not show that stack at first. It subscribed
to the page and never to the pthread workers, which appear only when the
module loads on the play press. It also dropped the `stackTrace` that an
error-level entry carries. Both are fixed in `tools/cdp_console.py` and
covered by `tests/test_cdp_console.py`. With the stack, the trap resolved to
`SDL_Emscripten_TimerHelper` called from `dynCall_vi`.

1. **The page asked the wrong question.** `navigator.gpu` exists in a Chrome
   with acceleration off; it is `requestAdapter()` that answers null. The page
   gate now awaits an adapter before booting the guest. A no-GPU browser now
   stops at setup with "This browser does not provide a WebGPU adapter", and a
   GPU browser passes the same page.
2. **The message box touched `document` from a worker.** SDL's Emscripten
   `ShowMessagebox` ran its DOM `EM_ASM` on the calling thread, which under
   `PROXY_TO_PTHREAD` is the guest worker. The SDL fork (eaab1e2) now proxies
   each DOM step to the main thread. The game's "Display failed!" is now a real
   dialog on the page. Its OK ends the run cleanly, and the page reads "The
   game closed (status 0)".
3. **A timer ran on freed memory.** `install_browser_log_sink` flushed the
   console from an SDL timer. The startup window probe in x2native calls
   `SDL_Quit`, and Emscripten's `SDL_QuitTimers` freed the entry while its
   browser timeout was still pending. In a GPU browser the freed bytes happened
   to survive, so the flush kept working by luck. In the no-GPU run they were
   reused, and the next firing called a garbage pointer. The SDL fork now
   clears each timeout before freeing its entry. The sink owns an
   `emscripten_set_interval` instead, so its flush no longer depends on SDL's
   lifetime.

Measured, with the page gate bypassed only in the served copy so the old route
could be reached: no-GPU Chrome, served wasm 10,068,983 bytes equal to the
build, zero traps, the dialog open on the page, and a clean exit after OK. GPU
Chrome on the restored build: the device was created, about 294 presents per
5 s, zero traps, and the log still flushing at 110 s.
