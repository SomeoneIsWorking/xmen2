---
id: C042
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: patches/xboxrecomp/0002-kernel-handle-and-ordinal-tables.patch
---

## Claim

bridge_read_handle() in the xboxrecomp kernel bridge had one indirection too many: it took a VA and loaded the token from Xbox memory, but all seven call sites pass the HANDLE by value (the Nt*File APIs take a HANDLE, not a PHANDLE).

## Evidence

gdb at kernel_bridge.c:1035 on the first NtQueryVolumeInformationFile: STACK_ARG(0) = 0x48000001 (a correctly tagged token), the deref of it = 0x0000F7FE, which fails the tag test and is passed through as a native HANDLE -- SIGSEGV in w32_handle_fd(h=0xf7fe). Taking the token by value, the same run completes NtOpenFile/NtQueryVolumeInformationFile/NtClose(handle=0x48000001) with no fault.

## What would falsify it

if any Xbox kernel file API is found that takes arg0 as a PHANDLE in-parameter, that call site needs the load back
