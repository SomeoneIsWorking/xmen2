---
id: C145
kind: claim
status: holds
created: 2026-08-07
tags: recomp,x87,translator,libIGGfx
---

## Claim

The x87 REGISTER store forms were both mistranslated: FST ST(i) was emitted as a pop (it does not pop), and FSTP ST(i) stored the POPPED value into X87_ST(i), which indexes the post-pop stack. 2981 FSTP ST(i) sites and 2 FST ST(i) in the image.

## Evidence

x87_fault now prints its caller's host return address; addr2line -i walked through the inlined x87_pop to fn_libIGGfx_100c2105 and the emitted line for '100c26b4 FSTP ST0'. The three bodies the ring named were read first and have proper MSVC prologues, so the wrong-function-boundary hypothesis was rejected rather than assumed. After emitting 'store then pop' (and no pop at all for FST) and re-emitting every module, the underflow is gone and the run stops on an ordinary missing body at XMen2.exe 0x0049f7e0. tests/test_recomp.py::X87RegisterStores covers all four cases.

## What would falsify it

an emitted chunk containing 'X87_ST(C, N) = x87_pop(C);' for a register-form FST/FSTP, or another x87_fault naming a register store
