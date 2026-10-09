/*
 * WHICH PRECISION THE GUEST'S x87 ARITHMETIC IS COMPUTED IN.
 *
 * Where the host has no x87 unit and its register file is the ext80
 * encoding -- the browser, and ARM64 Linux and Android -- extended precision
 * is software, and it was a fifth of the browser's guest worker (#162). The
 * game already runs at binary64 on Apple Silicon, whose registers are
 * doubles, with nothing a player can see broken, so `x87.double` (on by
 * default) gives the same precision to the other hosts with no x87 unit.
 * An x87 host keeps its exact unit: the mode is refused there.
 */
#pragma once

struct X86pCpu;

namespace x2::native {

/* Selects `cpu`'s arithmetic from `x87.double`. Called for every CPU that
   runs guest code, beside the census attach. */
void x86_engine_x87_precision_attach(struct X86pCpu *cpu);

} // namespace x2::native
