---
id: C096
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,host,rc-exe
---

## Claim

The native run clears every function-discovery blocker; what stops it now is the Win32 import surface

## Evidence

tools/native_discover.sh CONVERGED for the first time: two rounds of seeding, then round 3 reported no missing constructor targets at all. Everything after that is host work, not translation. Implemented this round, each actually doing the job rather than returning a plausible value: CreateSemaphoreA/ReleaseSemaphore, CreateEventA/SetEvent/ResetEvent, CreateMutexA/ReleaseMutex, WaitForSingleObject/WaitForMultipleObjects, TlsAlloc/Free/Get/Set, InterlockedExchange, QueryPerformanceFrequency (1e9, to agree with the existing QueryPerformanceCounter -- a mismatched pair is a timing bug that reads as a gameplay bug), GetCurrentProcess/GetCurrentThread pseudo-handles, DuplicateHandle including the DuplicateHandle(GetCurrentThread()) idiom, OutputDebugStringA, and LoadLibraryA/GetProcAddress answered from the module table. MEASURED: distinct (entry point, module) pairs entered 1611 -> 2044. Battery 33/33, ctest 4/4. The waits do NOT fake success: with no guest thread able to signal them, a blocking wait reports that nothing could ever satisfy it and stops, because returning WAIT_OBJECT_0 would hand the game a lock it does not hold. GetProcAddress on a natively-implemented system DLL returns a real thunk or an honest NULL -- observed working, the guest probed TryEnterCriticalSection, got NULL and carried on.

## What would falsify it

The synchronisation objects are modelled for a single guest thread. Every one of them becomes wrong the moment a second thread exists -- the mutex in particular always succeeds because the only thread must be the owner. If _beginthreadex is ever implemented, these must become real POSIX primitives before anything is believed about them.
