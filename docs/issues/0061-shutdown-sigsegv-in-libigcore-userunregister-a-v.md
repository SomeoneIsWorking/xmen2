---
id: 61
title: Shutdown SIGSEGV in libIGCore userUnregister: a virtual call returns NULL and the caller dereferences it
status: resolved
symptom: The native --d3d8 run exits 3 instead of 0 after a clean X2_MAX_FRAMES stop. SIGSEGV at 0x4, addr2line names fn_libIGCore_100517b0 = Gap::Core::userUnregister. Vulkan also reports VkSurfaceKHR not destroyed at vkDestroyInstance, which is the same teardown cut short.
tags: pc,native,jit,shutdown,libIGCore
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

Other resolved initialization and teardown failures also dereferenced NULL,
but none had this cause. A separate fault at 0x4 in
igMemoryPool::trimAll was caused by ordering rather than a return value.

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
      an IDENTITY problem -- which this port made plausible, since every guest
      thread was then a coroutine on ONE host thread, so
      pthread_self() cannot distinguish them.

Note that (b) cuts BOTH ways and is not obviously the culprit: sharing one host
thread should make the match MORE likely, not less, because every guest thread
now looks like whichever one registered first.

The next step is to print the count and the ids at the fault, not to guess
between them.

### Note (2026-08-12)
INSTRUMENT ADDED, and one mistake made and undone.

guest_engine_thread_report() (src/native/threads.c) reads the igThreadManager
out of guest memory and prints the array address, the count, and each thread's
id and refcount -- at zero as well, with the addresses that produced it.

It was FIRST written to match the module name 'libIGCore' and reported 'NOT
linked into this build' on a build that links it: modules register their FILE
name, 'libIGCore.dll'. That is an instrument that lied, and it lied in the
direction of 'nothing to see'. It now compares case-insensitively with the
extension, and when it finds nothing it LISTS the module names present, so the
same failure cannot be silent twice.

It was also called from the SIGSEGV handler, because that is exactly the moment
worth measuring. That was wrong and is reverted: it uses printf, and stdio in a
signal handler deadlocks on a lock the interrupted code may hold -- the reason
the SIGTERM report already writes with write(2). The observed
result was worse than a deadlock: the process took a SECOND SIGSEGV inside the
handler and exited 139 with NO report at all, turning a fault that named its own
function into a silent one. Getting this at a fault needs a write(2) formatter.

MEASURED so far, on a CLEAN run (exit 0, X2_MAX_FRAMES=2800): the singleton is
NULL at the shutdown report. That is consistent with userUnregister having run
normally and cleared it -- it writes 0 there on its way out -- so it does not
yet separate the two candidates. The crash is INTERMITTENT: the same gate run
crashed once and passed once with no change in between, which is worth knowing
before anyone reads a single clean run as a fix.

SEPARATE DEFECT SEEN ON THE WAY, not yet filed: with X2_MAX_FRAMES=400 the run
printed 'X2_MAX_FRAMES reached (4244 presented)' and kept printing it once per
frame without stopping, until the timeout killed it. The clean stop does not
stop at the limit it names.

### Note (2026-08-12)
The diagnostic itself CRASHED TWICE before it was safe, and that is the finding.

Both faults were inside guest_engine_thread_report, in the SHUTDOWN report --
the one place a diagnostic must not fault, because it takes every other report
down with it. And they were hidden: X2_MAX_FRAMES printed its 'reached' line
once per frame (3,847 copies in one run) and the fault report was buried in
them, so a report that CRASHED looked like a report that hung, and I described
it as a hang. The line is now said once, and with it the crash was one grep
away.

Cause: the report dereferenced guest pointers it had not checked. Bounds
checks were then added for mgr, arr and the elements -- but NOT for the slot
address itself, so it faulted a second time on the very first read. Checking
the second pointer and trusting the first is not a bounds check.

With the slot checked, the run exits 0 and the report says what it could not
do:

    engine threads: libIGCore.dll is mapped at 0x563f50c5b1c8, which is above
    4 GB, so its 32-bit guest addresses cannot be formed here. NOTHING was
    read.

That is a REAL question, not just a refusal: an earlier run of the same code
computed a plausible guest slot of 0x72080600, so X86Module::base is sometimes
below 4 GB and sometimes not. Either base is not the guest mapped address for
every module, or libIGCore.dll is reached some other way. Until that is
answered the engine-side measurement for this issue cannot be taken at all --
and the intermittency of the ORIGINAL crash may or may not be related, which
is a question to ask, not an answer to assume.

### Note (2026-08-12)
MEASURED AT LAST, and candidate (a) is OUT.

    engine threads: igThreadManager 0x00a8a098, array 0x00a8a0b0,
                    1 thread(s) registered:
              [0] thread 0x00a8aa60  id 0x7100a2a8  refcount 1

The list is NOT empty, so this is not an ordering problem in which teardown
empties the array before userUnregister looks. It is candidate (b): the list
had an entry and getCallingThread did not match it.

Three wrong turns got here, all in the diagnostic rather than in the defect,
and each produced a confident wrong answer:

1. X86Module::base is a POINTER TO the guest base. Reading (uintptr_t)m->base
   gave the host address of the runtime module's g_imgbase global, so the
   report announced "libIGCore.dll is mapped at 0x563f50c5b1c8, above 4 GB" --
   true about the wrong quantity. Every other user in the codebase writes
   *m->base; the header now says so, with the cost noted.
2. Before that, the same mistake reached RD32 and faulted TWICE inside the
   SHUTDOWN report, taking every other report down with it.
3. Then a RANGE check refused a perfectly good pointer. The manager is at
   0x00a8a098, past XMen2.exe's SizeOfImage (0x006744c6, confirmed against the
   PE header) and outside the guest heap, because the engine allocates it from
   its OWN pool. "Not in a range I know about" was reported as "the layout must
   be wrong". The check is now x86_peek32 over process_vm_readv, which answers
   "is it mapped" -- the question actually being asked -- and cannot fault.

NEXT, and now specific rather than a choice between guesses. Only ONE thread is
registered while the run creates six guest threads. If pthread_self
(FUN_10075400, the pthreads-win32 copy vendored into libIGCore) resolves
through guest TLS, then this port's per-coroutine TLS switching would hand each
guest coroutine a DIFFERENT handle even though they share one host thread, and
getCallingThread would fail to match whichever thread registered. Read
FUN_10075400's table lookup; do not assume it, because the naive expectation
runs the other way -- one host thread should make every guest thread look
identical, not distinct.

### Note (2026-08-12)
PREMISE CONFIRMED BY READING THE IMAGE: pthread_self resolves through Win32 TLS.

    FUN_10075400 (pthread_self)
      -> loads the key at 0x1015f4d8
      -> FUN_10075ff0
      -> CALL [0x10077048]

and walking libIGCore.dll's import table, IAT rva 0x77048 is
KERNEL32.dll!TlsGetValue. If the lookup returns NULL, pthread_self ALLOCATES a
fresh 0x38-byte handle for the caller.

So the vendored pthreads-win32 identifies a thread by Win32 TLS, and this port
switches Win32 TLS per guest coroutine (k32_tls_switch in kernel32.c). Each
guest coroutine therefore gets its own pthread handle -- which is the CORRECT
emulation, because a coroutine here stands for a Windows thread, and on Windows
each thread genuinely has its own TLS.

That matters for how this is fixed. Because the emulation is right, the port
should behave as Windows does, and on Windows igThreadManager::userRegister and
::userUnregister both run on the main thread, so getCallingThread matches.
Something in THIS port makes the unregistering coroutine differ from the
registering one, or makes the main coroutine's TLS differ between init and
shutdown -- for example if a slot is reused after a guest thread exits.

DO NOT "fix" this by making pthread_self return a process-wide constant. That
would paper over the mismatch and would break any engine code that legitimately
distinguishes threads, which is most of what a thread manager is for.

NEXT MEASUREMENT, now cheap: read the TLS key at 0x1015f4d8 and print, per TLS
slot, the pthread handle each guest coroutine holds -- alongside the one
registered thread's id (0x7100a2a8, measured). That says directly whether the
shutdown caller is a different coroutine or the same one with different TLS,
and those need different fixes.

### Note (2026-08-12)
ROOT CAUSE FOUND AND FIXED. It was candidate (b), and it was ours.

The measurement that settled it, printing the pthread handle each guest
coroutine holds beside the registered thread's id:

    engine threads: igThreadManager 0x00a8a098, array 0x00a8a0b0,
                    1 thread(s) registered:
              [0] thread 0x00a8aa60  id 0x7100a2a8  refcount 1
              pthread handles by guest-thread slot (TLS index 2):
                slot 0  handle 0x7100a2a8
                slot 16 handle 0x7125cc28

The registered thread's id is the handle in TLS slot 0. The MAIN thread is slot
16 and held a DIFFERENT handle. Same guest thread, two TLS arrays.

Why: kernel32.c initialised its TLS pointer to g_tls_store[0], and the main
thread ran on that array until the scheduler attached the main thread and
switched it to slot 16. Everything the main thread wrote to TLS before that
point went into slot 0 and then became invisible to it. The engine's
pthread_self is TlsGetValue, so igThreadManager::userRegister stored its handle
in slot 0 early; at shutdown the main thread was on slot 16, found nothing,
allocated a fresh handle, and getCallingThread matched neither -- returning
NULL into a caller that dereferences it.

Slot 0 is also guest thread 0's slot, so the old default did not merely
misplace the main thread's TLS: it ALIASED it onto the first guest thread's.
That is a correctness bug well beyond this crash and it is the reason the crash
was intermittent -- whether it bit depended on what guest thread 0 had done to
the shared array.

Fix: kernel32.c starts on the main thread's own slot, via GUEST_MAIN_TLS_SLOT
in threads.h, with a #error in threads.c if the two files ever disagree.

VERIFIED. After the fix the same report reads:

              pthread handles by guest-thread slot (TLS index 2):
                slot 16 handle 0x7100a2a8

-- one handle, on the main thread, equal to the registered thread's id. Three
smoke_loop runs: 0 SIGSEGV in any (the gate now FAILS on a crash, so this is
checked rather than tolerated), 2 PASSED, 1 hit its own 400s timeout with its
reports fully written and no fault. That timeout is NOT explained by this fix
and should not be filed under it.

### Resolution (2026-08-12)
The main thread ran on TLS slot 0 until the scheduler attached it to slot 16, so everything it put in TLS before that -- including the engine pthread_self handle -- became invisible to it, and getCallingThread returned NULL into a caller that dereferences it. Slot 0 is also guest thread 0's, so the old default aliased the two. kernel32.c now starts on the main thread's own slot (GUEST_MAIN_TLS_SLOT, with a #error if the two files disagree). Verified: the registered thread's id and the main thread's handle are now the same value 0x7100a2a8, and three smoke_loop runs produced no SIGSEGV.
