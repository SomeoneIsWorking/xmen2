---
id: C122
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,rc-lift,rc-exe,exceptions
---

## Claim

The exe's 'missing indirect call targets' inside SEH functions are MSVC C++ CATCH FUNCLETS, which are neither ordinary functions nor safe to seed

## Evidence

Chased from a concrete failure. FUN_005fac10 begins PUSH -0x1; PUSH 0x679141; MOV EAX,FS:[0x0] -- an MSVC exception-frame prologue -- and 0x679141 is MOV EAX,0x6c3d58; JMP 0x0067208c, where 0x0067208c is JMP dword ptr [0x0067f154] and XMen2.iat resolves 0x0067f154 to MSVCR71.dll __CxxFrameHandler. That is the canonical C++ EH stub: load the FuncInfo pointer, jump to the handler. So this function has try/catch, and 0x6c3d58 is its FuncInfo. The runtime then reports an indirect call to 0x005fb270, an address INSIDE that function's range which Ghidra places in no function at all -- i.e. a catch funclet, whose address lives only in the FuncInfo TryBlockMap (DATA) and which MSVCR71's unwinder calls indirectly. Two properties make this its own category: (1) no reference-driven static pass can find it, because the only reference is a data table, which is exactly why the discovery loop keeps reporting these; and (2) it CANNOT be seeded as an ordinary function, because a funclet runs on its PARENT's frame -- seeding carves the parent apart, and doing so here across three sessions split one 426-instruction function into five fragments whose RETs then popped pieces of the exception registration record (issue #21). Confirmed by control: with the boundaries repaired the accidental resolution disappears and the run stops EARLIER, at the indirect call itself.

## What would falsify it

Parsing the FuncInfo at 0x6c3d58 (magic 0x19930520/21/22, nTryBlocks at +0xc, TryBlockMap at +0x10, HandlerType.addressOfHandler at +0xc of each 0x10-byte entry) and NOT finding 0x005fb270 among the handler addresses. That parse was attempted here and failed on a VA-to-file-offset bug in a throwaway script, not on the data -- so it is UNVERIFIED and is the first thing to do. If 0x005fb270 is absent from every TryBlockMap in the image, this claim is wrong and the address is something else.
