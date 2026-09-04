---
id: C116
kind: claim
status: holds
created: 2026-08-06
tags: ark,vtable,vulkan
---

## Claim

igObject's virtual interface is 21 slots and 17 of them are DO-NOTHING stubs, so a host class implementing it owes almost no behaviour. Read from libIGCore, which unlike every other shipped module still carries real symbols. Measured on igErrorHandler (instance size 0x8 = vptr + refcount, i.e. igObject plus nothing), whose 21-slot vtable is filled by only FIVE distinct addresses: 0x10020770 (body: RET) in 10 slots; 0x1004f580 (body: RET 0x4) in 7 slots; 0x100459d0 (body: MOV AL,1 / RET 0x4, i.e. return true) in slots 5 and 6; 0x10046d00 igObject::createCopy in slot 19; 0x1000eba0 getClassMeta in slot 20. The collapse is MSVC identical COMDAT folding: every identical trivial body is merged to one address, which is also why a by-name slot report on this binary looks incoherent while being correct. Practical consequence: slots taking no stack argument are RET, slots taking one are RET 4, and a host class need only supply getClassMeta, createCopy and the two return-true predicates.

## Evidence

`tools/ark_classes.py` reads the defining module because libIGCore calls
`igArkRegister` directly rather than through an IAT slot: 169 classes were
recovered, with zero unrecovered argument lists and zero
`isAbstract`/`retrieveVTablePointer == NULL` disagreements.
`tools/ark_vtables.py` recovered 130 vtable addresses from the authenticated
libIGCore image. `igErrorHandler` was selected because its 0x8-byte instance is
`igObject` plus no fields; the folded function bodies were checked in the
retail instruction stream.

## What would falsify it

Finding a slot whose folded body is RET/RET 4 but whose real contract returns a value the caller uses -- ICF hides that, since the folded body is shared with genuinely-void methods. Implementing the 21 and having construction still fail would show the map is incomplete.
