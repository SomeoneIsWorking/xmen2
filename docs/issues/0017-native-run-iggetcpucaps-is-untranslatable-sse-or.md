---
id: 17
title: Native run: igGetCPUCaps is untranslatable (SSE ORPS), and it is the routine that detects SSE
status: resolved
symptom: x86_untranslated: reached 0x10067870 Gap::Core::igGetCPUCaps -- blocked by: mnemonic ORPS. Reached after the CRT varargs family was implemented
tags: pc,recomp,translator,sse,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Where it stops

The frontier after the host-import work. `igGetCPUCaps` uses SSE instructions to
detect SSE support, so it cannot be translated by a model with no XMM state.

Translator coverage is otherwise very close to complete:

    libIGCore   5964 of 5968 functions (99.9%)   blocked: 3x RCR, 1x ORPS
    XMen2.exe   blocked: MOVUPS, PMULLW, PUNPCKLDQ (SSE/MMX),
                PFADD/PFSUBR/PSWAPD/PI2FD (3DNow!, an AMD path),
                CMPSW.REPE, PUSHAD, ENTER, AAS, RCR

## The decision this forces

Three options, and picking one carelessly would be a hack:

1. **Add XMM state and the SSE instructions to the translator.** The honest,
   general fix. Sized but not small: the CPU struct has no 128-bit registers.
2. **A native override for `igGetCPUCaps`.** The project's intended architecture
   for exactly this (see the recomp-overrides methodology), and defensible --
   but only once what the function RETURNS is actually reverse-engineered, or
   it is faking a step's output, which the re-frontier rule forbids.
3. **Report a reduced feature set from CPUID** so the guest never takes the SSE
   path. Tempting and cheap. It is arguably the most honest answer -- the
   recompiled "CPU" genuinely does not implement SSE -- but it changes which
   code path the game takes versus the original PC build, so it must be a
   recorded decision with that consequence stated, not a quiet workaround.

The cheap mnemonics (RCR, PUSHAD, ENTER, AAS, CMPSW.REPE) are ordinary
translator work and independent of this choice.

### Resolution (2026-08-06)
Resolved differently from all three options the issue listed -- none of them was necessary. The premise was wrong: recomp.py refused the WHOLE function for one unsupported instruction, so igGetCPUCaps's 897 translatable instructions were blocked by one SSE instruction in a case the engine never asks for (it calls indices 0 and 1 only). An unsupported instruction is now emitted in place as a call that aborts by name if executed, which keeps the loud-failure guarantee exactly where it matters and stops blocking unreached code. No CPUID masking, no premature override, no XMM work. A second defect surfaced immediately behind it: the function's computed JMP (a 59-case switch) dispatched globally to 0x1006790e, an address inside the function itself, which is jump-table entry [0]. Functions with an indirect JMP now carry a local dispatcher. One bug of mine on the way: the first version switched on the linked address, but jump tables live in .rdata and are RELOCATED, so at run time the entry reads 0x2406790e -- it now switches on the offset from the module base, which also makes the case labels constant again. VERIFIED: the run clears igGetCPUCaps; libIGCore emits 5968 of 5968 functions; pairs entered 2121 -> 2314. Now stops on MSVCRT!fflush, an ordinary host import.
