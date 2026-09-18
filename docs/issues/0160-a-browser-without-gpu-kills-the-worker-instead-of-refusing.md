# 0160 — a browser with no GPU kills the guest worker instead of refusing

- **State items:** S021
- **Status:** reproduced; not yet diagnosed to a call site

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
