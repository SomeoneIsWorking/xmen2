---
id: C115
kind: claim
status: holds
created: 2026-08-06
tags: ark,vulkan,graphics
---

## Claim

A HOST-defined class can register with ARK and be constructed by libIGCore: C008's falsifier is now exercised, not merely read. Running against the real game, igArkRegister accepts eleven arguments built entirely by native C, fills in our igMetaObject* slot, and returns with the guest stack balanced; igMetaObject::createInstance then allocates from the engine's pool and igObject::constructDerived stamps OUR vtable pointer into the instance and dispatches through it. That last step is C009 observed rather than argued: constructDerived does MOV [ESI + ECX], EAX where ECX is the vptr offset read from igArkCore+0x394 and EAX is [meta+0x5c], the value our retrieveVTablePointer returned. TWO constraints were discovered by faulting, and both are permanent design facts. (1) Registration CANNOT happen at module-init time: igArkRegister calls igGetMemoryPool, and the engine's pools do not exist until the exe's startup has run, so registering from DllMain faults inside libIGCore dereferencing a NULL pool. It must be triggered at a moment during the run. (2) Every native callback the guest CALLS must pop its return address, exactly as the import stubs' ret_std does; omitting it drifts the guest stack four bytes per call, and two such calls during registration shifted igArkRegister's eleven arguments by eight bytes so that it read arkRegisterInitialize as its dependentArkRegisters and faulted two hundred instructions later in an unrelated function.

## Evidence

scratch/build-native/x2native --no-window --ark-probe against the real install. The boundary ring shows retrieveVTablePointer and arkRegisterInitialize each crossing +4 (balanced), then Gap::Core::igArkRegister returning +4, then igArenaMemoryPool::malloc, then a dispatch landing in the host class's own vtable. src/vulkan/igvk_probe.c checks the meta is non-NULL, that meta+0x48 holds the instance size we passed and meta+0x1a is 0, and that the constructed object's vptr equals the exact vtable address handed over -- not merely that nothing crashed.

## What would falsify it

The probe does not yet print PROBE PASSED: construction keeps dispatching vtable slots the probe has not implemented (slot 20 satisfied, now slot 1), so 'libIGCore constructs a host class' is proven up to and including the vptr stamp and first dispatches, NOT through a complete construction. Falsified if a required slot turns out to need engine state a host class cannot supply, or if the object is rejected later by code that inspects it.
