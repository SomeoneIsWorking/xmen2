---
id: 107
title: Session-only controller could enumerate but never receive player bindings
status: resolved
symptom: A controller with no SDL serial or stable path appears in the assignment grid but Start and every pad binding remain missing because stable-only resolution makes the device unusable
tags: input,controller,hotswap,identity,session,assignment
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

Rejecting non-stable identities from persistent lookup correctly stopped slot reuse from stealing a saved reservation, but the same lookup was also the only runtime assignment path. The UI refused the row entirely, conflating cannot persist with cannot play now. X2_VIRTUAL_PAD exposed the defect: the pad enumerated while Pause/Start was missing from all three player binding banks.

## Proper resolution

A separate process-lifetime assignment owner binds manual session assignments to immutable live instance GUIDs. It must never serialize, must remain assigned-but-unresolved after disconnect, must never fall back to persisted identity or a reused slot, and must feed the same player publication/join policy as stable assignments.

### Resolution (2026-08-22)
Added a process-lifetime assignment owner keyed by immutable live GUID. The grid assigns/clears session-only pads without saving; player publication gives the transient owner precedence even while disconnected, suppressing persisted fallback and slot reuse until explicit clear. Assignment provenance is independent of identity quality, so a stable-ID pad assigned through the live control route remains labeled and cleared as a session overlay instead of mutating persistent settings. Tests prove immediate bindings and Start join, P1 hotswap, P2-P4 single-device override, disconnect reservation, persisted restore after clear, no serialization, different-GUID slot reuse refusal, and stable-pad transient routing.
