---
id: C149
kind: claim
status: holds
created: 2026-08-11
tags: pe,relocations,loader
---

## Claim

A relocatable PE module's relocation table enumerates its stored absolute code
pointers; the runtime loader must relocate those values before execution.

## Evidence

Every stored absolute address in a relocatable PE image needs an
`IMAGE_REL_BASED_HIGHLOW` entry so the loader can adjust it when the module
moves. In the shipped images, 4,396 relocation values in msdia80, 2,307 in
libIGCore, and 1,764 in libIGOpt point into `.text`; msdia80 addresses
0x103f06fa and 0x103f0715 are examples. XMen2.exe is linked `/FIXED` and has
no relocation directory, so this claim does not apply to it.

## What would falsify it

A stored absolute pointer that changes when its module moves despite lacking a
matching relocation, or a relocation classified as a code pointer whose value
does not land in executable image bytes, would invalidate the inventory.
