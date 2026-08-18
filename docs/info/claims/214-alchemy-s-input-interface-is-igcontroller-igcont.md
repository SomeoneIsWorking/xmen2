---
id: C214
kind: claim
status: holds
created: 2026-08-18
tags: 
---

## Claim

Alchemy's input interface is igController/igControllerManager and a port supplies the concrete delegate, not the interface

## Evidence

tools/ark_classes.py over libIGDisplay.json + libIGDisplay.dll + its IAT: 19 classes recovered at 19 igArkRegister call sites across 549 functions, 0 sites whose arguments could not be fully recovered, 0 disagreements between the isAbstract argument and the independent retrieveVTablePointer==NULL reading. Four abstract->concrete bindings via _Meta+0x3c, all recovered: igController->igWin32Controller, igControllerManager->igWin32ControllerManager, igInterfaceManager->igDefaultInterfaceManager, igWindow->igWin32Window. Sizes: abstract igController 16 bytes vs concrete igWin32Controller 148; igControllerManager 12 vs igWin32ControllerManager 12. Written up in shared/alchemy docs/RE/input.md.

## What would falsify it

a build of Alchemy in which igController or igControllerManager registers with a NULL _Meta+0x3c delegate, or in which the concrete controller the manager hands out is not the registered delegate
