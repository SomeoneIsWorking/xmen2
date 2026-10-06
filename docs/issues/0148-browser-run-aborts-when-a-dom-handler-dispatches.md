---
id: 148
title: Browser run aborts when a DOM handler dispatches to a thread whose mailbox closed
status: resolved
symptom: browser run stops with Assertion failed: false && emscripten_proxy_async failed at html5/callback.c:40 _emscripten_run_callback_on_thread
tags: web,browser,wasm,sdl,threads
created: 2026-09-14
updated: 2026-09-24
---

## Symptom

The deployed browser build (source `f1cf2a3`) stops a few seconds into boot with

```
The game stopped: Assertion failed: false && "emscripten_proxy_async failed", at:
/emsdk/emscripten/system/lib/html5/callback.c,40,_emscripten_run_callback_on_thread
```

The console's last line before it is the guest's own
`DINPUT: EnumDevices(devType=3 KEYBOARD, ...)`; nothing is logged between them.

## Cause

`_emscripten_run_callback_on_thread` asserts when `emscripten_proxy_async` cannot
enqueue. The enqueue fails only in `em_task_queue_send` ->
`emscripten_thread_mailbox_ref`, which returns 0 once `mailbox_refcount == 0`,
i.e. **the target thread has exited** (`_emscripten_thread_mailbox_shutdown`).
The assert text reaches the page because this build's `___assert_fail` calls
`abort("Assertion failed: ...")`, and `_abort_js` forwards that string to
`Module.onAbort` (app.mjs prints it as "The game stopped: ...").

Whether a DOM handler proxies or calls the C callback directly is decided at
registration time, in the shipped glue:

```js
getTargetThreadForEventCallback(targetThread) {
  switch (targetThread) { case 1: return 0; case 2: return PThread.currentProxiedOperationCallerThread; default: return targetThread }
}
registerGamepadEventCallback = (…, targetThread) => {
  targetThread = JSEvents.getTargetThreadForEventCallback(targetThread);
  … if (targetThread) __emscripten_run_callback_on_thread(targetThread, callbackfunc, …)
    else dynCall_iiii(callbackfunc, …)
}
```

Emscripten's `emscripten_set_*_callback` wrappers pass
`EM_CALLBACK_THREAD_CONTEXT_CALLING_THREAD` (= 2), so the stored target is
`PThread.currentProxiedOperationCallerThread` — a real pthread pointer whenever
the registration ran inside a proxied operation. `-sPROXY_TO_PTHREAD=1` makes
that the case here: the proxy main thread is a worker, so SDL's registrations
proxy to the browser main thread and each one records a pthread as its dispatch
target. SDL's Emscripten joystick backend registers
`gamepadconnected`/`gamepaddisconnected` in `EMSCRIPTEN_JoystickInit`, which the
port reaches lazily from guest device enumeration
(`dinput8.c::m_EnumDevices` -> `dinput_pad_refresh` ->
`SDL_InitSubSystem(SDL_INIT_GAMEPAD)`), and its video path registers the whole
key/mouse/focus/resize/visibility/pressure set. Once that thread exits, the next
matching DOM event dispatches into a closed mailbox and aborts the page.

Measured on the deployed build in headless Chromium (WebLua, real 2.37 GB
install): wrapping `EventTarget.prototype.addEventListener` before the runtime
starts recorded 56 registrations, and **every one of them carried
`__emscripten_receive_on_main_thread_js` in its stack** — i.e. every handler was
registered on the main browser thread on behalf of a worker and therefore holds
a non-null thread target. Invoking the recorded listeners directly with
synthetic events (`gamepadconnected` with a fake `Gamepad` object, plus
`visibilitychange`, `resize`, `keydown`, `mousemove`, `wheel`, pointer and drag
events) did **not** assert in that run, so its targets were still live; which
thread's mailbox was closed in the failing run is not yet identified.

## Repro

1. `./run.sh` -> live site, import a real ZIP (or use a saved install), click
   Play. Two independent runs of the deployed build stopped on their own with
   `The game stopped: native code called abort()` (a plain `abort()`, this time
   reported by `Pthread 0x01b722c8 sent an error!`), the guest having executed
   37.4M blocks / 501,717 translated with 0 refusals, presenting 1 frame in 75 s
   and then spinning: "the guest is running, but not reaching Present".
2. So the browser run is not merely being killed by the assert: it also ends in
   a native abort with the guest stalled after a single presented frame.

## Correction

The missing piece was which thread owns a registration. In the browser every
guest thread has a pthread of its own (`src/native/threads.c`), and SDL's
video, joystick and sensor backends bind their DOM listeners to whichever
thread first starts the subsystem. Those starts were lazy:
- pads from the thread that first enumerates devices, and sensors when a pad
  with sensors opens;
- video from the renderer's `CreateDevice` caller, after the startup window
  probe had torn the first start down with `SDL_Quit`.

Any guest thread that started one and later exited left every matching DOM
event addressed to a closed mailbox. The same class also explains the second
symptom here: a plain `abort()` on a worker, a stalled guest.

`sdl_host_setup` now starts video, sensor and gamepad in the browser, before
any guest code runs, on the thread that runs `x2native_main`. That thread
outlives every guest thread, and the runtime removes its listeners when it
exits. The gamepad start goes through the pad layer's one
`dinput_pad_subsystem_start()`, so the background-events policy is set first,
as it is for the inventory and the synthetic pad. The window probe now
releases only its own video reference. Every later start is a reference count
and registers nothing.

Measured on the Dead Zone route with the new build (served wasm 10,069,043
bytes): the device was created and the route presented about 274 frames per
5 s. A synthetic sweep at 70 s (keydown, keyup and mousemove on window,
document and canvas, plus resize, visibilitychange and a fake
`gamepadconnected`) aborted nothing. The same sweep after the runtime exited
(the no-GPU route, after its dialog's OK) aborted nothing either.

Not reproduced: the failing run's exact thread. The deployed build from
`f1cf2a3` is long gone, and this route's guest has one thread. The fix
removes the class rather than the one instance: no DOM-listening subsystem
can now be started by a thread that can exit before the page.
