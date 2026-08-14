---
id: 68
title: Hosted tail dispatcher defers a nested virtual tail jump and corrupts the caller stack
status: resolved
symptom: Wine-hosted x2run reaches MSVCR71 __security_error_handler after the renderer starts; FUN_005c7a00 checks a pointer instead of the security cookie and exits before a stable rendered frame
tags: pc,recomp,hybrid,runtime,abi,stack,tail-call
created: 2026-08-14
updated: 2026-08-14
---

## Root cause

The hosted dispatcher kept one global pending-tail pointer active for the entire outer dispatch. A directly called thunk such as FUN_005bbd10 tail-jumps through a vtable. Because the outer pointer was still active, x86_tail_dispatch only queued that target and returned immediately. Its direct caller continued before the virtual target ran. In FUN_005c7a00 this left ESP four bytes low at 0x005c7c40, so the security-cookie load read the adjacent object pointer and the real CRT correctly called __security_error_handler.

## Evidence

Before the fix, FUN_005c7a00 entered at ESP 0x0399ecd4 and the cookie checker entered at 0x0399ec84 with ECX equal to the object pointer, one dword below the expected frame position. The tail thunk FUN_005bbd10 entered, but its virtual target ran only after the caller had continued.

The fix tracks generated-body depth in CPU and only iterates a tail target when it belongs to the current dispatch frame. A tail jump from a deeper, directly called body opens a nested dispatcher and completes before the caller resumes. Generated tail exits retire their logical body depth.

After the fix, FUN_005c7a00 enters at 0x039eecd4 and returns at 0x039eecd8, exact RET balance; its callees return to the expected frame positions, __security_error_handler is never called, x2run remains alive for the 15-second run, and two captured frames contain 593 and 1098 colors.

## Regression coverage

- tests/test_recomp_hosted.py checks depth-aware nested dispatch and state restoration.
- tests/test_recomp.py requires emitted indirect tail jumps to retire their source body.
- The real WATCH=1 x2run run is the shipping-path discriminator; it exercises the vtable tail thunk that exposed the defect.
