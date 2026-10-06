---
id: 111
title: Host resolution coupled into the retail registry changed the logical backbuffer
status: resolved
symptom: A 3840x2160 host setting changed the guest D3D backbuffer from the retail 800x600 mode to 3840x2160 and visibly shrank game text
tags: pc,native,registry,resolution,settings
created: 2026-08-24
updated: 2026-08-24
---

## Cause

The added `registry_view` treated the retail `Display/Resolution` value as a
synthetic view of `x2native.conf`. The retail engine consumes that registry
value to choose its logical D3D backbuffer, so exposing the host output size
there did not merely resize the window or swapchain: it changed game-space
resolution. The implementation incorrectly made two distinct policies share
one authority.

## Resolution

Removed `registry_view`, its shipping-boundary test, and all ADVAPI coupling,
restoring `src/native/advapi32.c` to its prior persistent retail behavior. Host
window/swapchain dimensions remain presentation policy and must scale or
compose the retail logical backbuffer externally; they must not rewrite the
guest registry value. C255 records the rejected shared-authority design and
the bounded run that falsified it.
