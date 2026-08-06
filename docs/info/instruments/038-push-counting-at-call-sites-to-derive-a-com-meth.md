---
id: I038
kind: instrument
status: DISTRUSTED (unsound for this purpose -- see below)
created: 2026-08-06
---

## Instrument

Push-counting at call sites to derive a COM method's argument count

## Validated by

TESTED AND FOUND UNSOUND for this purpose, which is why it is recorded rather than used. Counting PUSH instructions immediately before each  in libIGGfx gives a consistent count for 103 of 195 device vtable offsets and DISAGREEING counts for 92 -- offset 0xc8 alone shows 1,2,3,4,5 and 6 pushes across its 100 call sites.

Two reasons, and the second is the important one. The backward scan stops at the wrong instruction when arguments are staged with MOVs into stack slots rather than pushes. And, fundamentally, THE SAME OFFSET BELONGS TO DIFFERENT INTERFACES: 0x4, 0x8 and 0xc are AddRef/Release on every COM object in the family, so a single offset legitimately has different signatures depending on which object the call is on.

This closes off a tempting shortcut. A host IDirect3DDevice8 built as a vtable of counted no-op stubs (the device-layer version of --vk-permissive) needs each stub to pop the right number of arguments, and this was the obvious way to get those counts without a D3D8 header. It does not work, and using it would shift the guest stack at every disagreeing offset.

It also demonstrates C128's falsifier empirically rather than as a prediction: the 46 offsets must be attributed to INTERFACES before any of them is implemented. The method that DOES work is the one used for slot 166 -- find a specific call site, read its pushes, and cite it -- applied per method with the interface known.

## Known failure modes

(none recorded yet)
