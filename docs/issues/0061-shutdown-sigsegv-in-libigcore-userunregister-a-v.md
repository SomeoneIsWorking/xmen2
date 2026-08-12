---
id: 61
title: Shutdown SIGSEGV in libIGCore userUnregister: a virtual call returns NULL and the caller dereferences it
status: open
symptom: The native --d3d8 run exits 3 instead of 0 after a clean X2_MAX_FRAMES stop. SIGSEGV at 0x4, addr2line names fn_libIGCore_100517b0 = Gap::Core::userUnregister. Vulkan also reports VkSurfaceKHR not destroyed at vkDestroyInstance, which is the same teardown cut short.
tags: pc,native,recomp,shutdown,libIGCore,rc-exe
created: 2026-08-12
updated: 2026-08-12
---

## What the guest code does

userUnregister (libIGCore 0x100517b0, 88 bytes):

    MOV ECX,[0x1015f438]      ; a singleton
    MOV EAX,[ECX]             ; its vtable
    CALL [EAX + 0x54]         ; -> EAX
    ...
    MOV ESI,EAX
    CALL [EDX + 0x5c]
    MOV EDX,[ESI + 0x4]       ; <-- FAULTS, ESI is 0

The register dump confirms it: esi 00000000, and the fault address is 0x4.

So the virtual call at slot 0x54 returned NULL where the guest requires an
object, and the next instruction takes its +4 field (a reference count -- the
code decrements it and calls a destructor at 0x10045ff0 when the low 23 bits
reach zero).

## Why this matters beyond the exit code

Teardown stops where it faults, which is why Vulkan then reports a VkSurfaceKHR
still alive at vkDestroyInstance. The Wayland queue warning about attached
proxies is the same event, not a second one.

## What has NOT been established

Which object 0x1015f438 holds, and what slot 0x54 is on its vtable. Until that
is read out of the image, whether the NULL comes from an override of ours or
from guest state is UNKNOWN -- and this project has had exactly this shape
before (issue #54, C158: an override that left EAX alone and the caller branched
on leftover register contents). The rule that applies is the one already
written down: an override must reproduce the original's RETURN VALUE, checked
at the CALL SITE.

## Related

Issues #20, #15, #14 are the same family (a NULL dereferenced during teardown
or init) and are all resolved; none is this one. #14 is also a fault at 0x4,
in igMemoryPool::trimAll, and its cause was ordering rather than a return
value.

### Note (2026-08-12)
RE done: the singleton at 0x1015f438 is the igThreadManager, and vtable slot 0x54 is getCallingThread -- getThreadManager (0x10006a50) reads the same address, and igThread::getCallingThread (0x100068e0) is three instructions: load it, load its vtable, tail-jump through +0x54.

igPthreadThreadManager::getCallingThread (0x10064700) is a LINEAR SEARCH and returns NULL by design when it finds nothing:

    self = pthread_self()            ; FUN_10075400, the pthreads-win32 one
                                     ;   vendored into libIGCore
    for i in 0 .. mgr[8][8]-1:       ; the thread array and its count
        t = mgr[8][0x10][i]
        if pthread_equal(self, t[0x40]): return t
    return NULL                      ; XOR EAX,EAX at 0x100647a5

userUnregister does not check the result: it takes [thread+4], the reference
count, and decrements it. So the fault means the calling thread was NOT in the
manager's list at shutdown.

Two candidates, NEITHER measured yet, and they are distinguishable:
  (a) the list is already EMPTY (count <= 0 jumps straight to the NULL return),
      i.e. an ORDERING problem in teardown;
  (b) the list is non-empty but no entry's +0x40 matches pthread_self(), i.e.
      an IDENTITY problem -- which this port makes plausible, since every guest
      thread is now a coroutine on ONE host thread (issue #57), so
      pthread_self() cannot distinguish them.

Note that (b) cuts BOTH ways and is not obviously the culprit: sharing one host
thread should make the match MORE likely, not less, because every guest thread
now looks like whichever one registered first.

The next step is to print the count and the ids at the fault, not to guess
between them.
