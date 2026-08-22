---
id: 98
title: Controller reconnect retargets stale DirectInput devices and eventually storms enumeration
status: resolved
symptom: After repeated controller disconnect/reconnect, hotswap becomes unreliable; an old device can follow a new controller in the same slot and after eight connection lifetimes re-enumeration repeats every poll
tags: pc,native,input,pad,dinput,hotswap,identity,lifecycle,user-report
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

`DInputDevice` stored the reusable `dinput_pad` inventory slot, not the live DirectInput instance GUID. A new controller reusing that slot therefore revived the guest COM object for the disconnected controller. Separately, `dinput8.c` kept a process-lifetime cache of eight offered GUIDs: eight is the simultaneous inventory limit, not a bound on connection lifetimes. Once full, new GUIDs were never admitted into the cache and every input poll retriggered enumeration.

Prompt prose and the RmlUi controller label had related ownership errors: prompt mode followed any connected pad instead of the resolved player/source, and the settings document snapshotted the inventory only when rebuilt for another reason.

## Resolution

Guest devices bind immutable live GUIDs and resolve that GUID through the current inventory on every operation. Disconnection leaves the old object input-lost; slot reuse creates a distinct object. A monotonic inventory generation advances on successful open/close, and a generation admission owner invokes the game re-enumeration routine once for both arrivals and removals without retaining lifetime GUIDs. Player assignment is a device grid keyed by stable controller identity; Player 1 alone may own keyboard plus controller for hotswap, and prompt family follows its last active source. Serial/path identities are persistent. A device exposing neither gets a per-lifetime session identity: it can be assigned for the current process but is never serialized or auto-matched. Its immutable live-GUID assignment remains unresolved after disconnect and suppresses fallback until explicitly cleared. The live probe reports identity quality with generation, live GUID and resolved ownership.

Production-seam tests cover attach/detach, immutable instance resolution, same-slot reuse, refusal of two identical virtual units as persistent identities, process-only assignment, disconnect suppression, non-serialization, and twelve reconnect cycles (more than the eight-pad simultaneous limit). Real physical-controller stable-identity validation remains open: this host exposes an Xbox Wireless Adapter but no paired controller or SDL gamepad device.
