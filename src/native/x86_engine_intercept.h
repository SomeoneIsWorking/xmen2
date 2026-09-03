/*
 * x86_engine_intercept.h -- when the runtime engine must hand an address back
 * to this dispatcher instead of executing it as guest x86.
 *
 * These are the predicates x86port's JIT calls between basic blocks
 * (x86p_jit_engine_set_intercept) and during translation
 * (x86p_jit_engine_set_boundary). The interpreter path in x2_engine_call makes
 * the same decision inline, one instruction at a time.
 *
 * Split from x86_engine.c so the decision -- and specifically its independence
 * from the interpreted-call frame stack -- can be tested without building the
 * whole engine. An earlier version gated the native-override check on
 * engine_frame_top() returning a frame, and that pointer goes NULL once
 * nesting passes ENGINE_FRAMES_MAX: every override reached through a boot call
 * chain deeper than 64 then ran as raw guest code under engine=jit (the font,
 * boot-splash and prompt-glyph overrides never fired; the native FMV override
 * did not either, so the intro played through the guest's MMX decoder).
 */
#ifndef X2_X86_ENGINE_INTERCEPT_H
#define X2_X86_ENGINE_INTERCEPT_H

#include <stdint.h>

struct X86pCpu;

/*
 * True when `eip` is host code this dispatcher owns -- an import thunk, the
 * engine's return trampoline, or a resolved native override body. Frame-stack
 * independent by construction.
 */
int x86_engine_intercepts_addr(uint32_t eip);

/*
 * x86port's between-blocks intercept hook. Adds the stack-relative return
 * check (stop the instant the current interpreted call returns) as a
 * refinement when a frame is on record; the address check itself is
 * unconditional.
 */
int x86_engine_jit_intercept(const struct X86pCpu *cpu, void *user);

/*
 * x86port's translation-time boundary hook: the pure-EIP subset, plus the
 * setjmp3 thunk (a RET already ends a block, so no return check is needed).
 */
int x86_engine_jit_boundary(uint32_t eip, void *user);

#endif /* X2_X86_ENGINE_INTERCEPT_H */
