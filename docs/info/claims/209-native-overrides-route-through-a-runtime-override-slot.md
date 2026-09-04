---
id: C209
kind: claim
status: holds
created: 2026-08-16
tags: override,jit,runtime
---

## Claim

Native overrides are declared in their owning subsystem with
`x86_register_override("<module>", linked_entry, fn)`. Runtime dispatch hands
those module-qualified entries to native code before translating the original
guest body, and `x86_guest_body` deliberately re-enters the original through
the JIT.

## Evidence

`x86_register_override` in `src/native/x86rt_native.c` records the module name,
linked entry point, and native function. After `pe_map` places every image,
`x86_overrides_resolve` converts each pair to a mapped address and refuses an
absent module, out-of-image address, or invalid entry. Runtime interception
checks import thunks and then registered native bodies. A native implementation
that wants retail behavior calls `x86_guest_body(C, module, linked_entry)`,
which scopes the ordinary override out and executes that body through the JIT.

The runtime selftests cover successful resolution plus absent-module,
out-of-image, and invalid-entry refusals. A tutorial boot observation reached
registered DirectX and frame-pacing overrides, loaded the level, and reported
zero refused draws.

## What would falsify it

A run reaching a registered module-qualified entry without handing it to the
native function, or `x86_guest_body` recursively re-entering that override
instead of executing the retail body through the JIT.
