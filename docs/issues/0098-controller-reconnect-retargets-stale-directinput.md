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

Guest devices bind immutable live GUIDs and resolve that GUID through the current inventory on every operation. Disconnection leaves the old object input-lost; slot reuse creates a distinct object. A monotonic inventory generation advances on successful open/close, and a generation admission owner invokes the game re-enumeration routine once for both arrivals and removals without retaining lifetime GUIDs. Player assignment is a device grid keyed by stable controller identity; keyboard plus controller is implicit hotswap, disconnected assignments remain reserved, and prompt family follows the last active assigned source. Serial/path identities are explicitly stable. A device exposing neither gets a per-lifetime session identity which is visible but cannot auto-match or be reserved. The live probe reports that quality with generation, live GUID and resolved ownership.

Production-seam tests cover attach/detach, immutable instance resolution, same-slot reuse, refusal of two identical virtual units as persistent identities, and twelve reconnect cycles (more than the eight-pad simultaneous limit). Real physical-controller stable-identity validation remains open.
