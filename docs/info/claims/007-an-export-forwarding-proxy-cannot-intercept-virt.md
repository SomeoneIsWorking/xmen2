---
id: C007
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

An export-forwarding proxy can only intercept calls that **cross the DLL
boundary through the import table**. Two kinds of call are invisible to it:

1. **Calls internal to the original DLL** — resolved to direct relative `call`s
   at link time, they never touch an import thunk.
2. **Virtual dispatch through an object** — the original DLL's constructor
   stores its own vtable pointer in the object, so every virtual call goes
   straight to the original code.

Therefore replacing *behaviour* requires owning object **construction**
(`_instantiateFromPool` and the constructors, which are genuine cross-boundary
exports), not intercepting method exports. This is why the plan replaces whole
classes rather than individual functions.

## Evidence

The mechanism is not inferred — it follows from how PE linking and the MSVC C++
object model work, and it is visible in the export table: the 150 `??_7...@6B@`
vftables are DATA exports, i.e. the vtables are the original DLL's own data, and
`_instantiateFromPool` is what hands out objects pointing at them.

The trace run is consistent with it but does **not** on its own prove it: in a
90s run, 9 of the 22 traced boundary symbols fired (igWindow::getClassTypeLazy,
arkRegister, _instantiateRefFromPool/_instantiateFromPool for igWindow /
igDefaultInterfaceManager / igControllerManager, plus igWin32Window::hideCursor)
and 13 did not — including `igInterfaceManager::handleEvents`, the per-frame
event pump, throughout a playing cinematic.

## What would falsify it

The *mechanism* would be falsified only by observing a virtual call arriving at
an export thunk after the original DLL constructed the object.

The *trace evidence* is weaker than it first looked and must not be cited as
proof. `igControllerManager::getController` is NON-virtual (QBE) and also never
fired, so "non-virtual symbols always fire" is false — absence can simply mean
the scenario never called it. Equally, the game's own igInterfaceManager
subclass may override `handleEvents` and never chain to the base, which would
explain that zero without any vtable argument. A clean discriminating experiment
is still owed: drive the game to a point where a known non-virtual boundary
symbol must be called, and confirm it fires while a virtual one stays at zero.
