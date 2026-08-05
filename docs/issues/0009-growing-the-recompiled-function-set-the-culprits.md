---
id: 9
title: Growing the recompiled function set: the culprits are independent, so one bisection is not enough
status: resolved
symptom: A hybrid libIG*.dll with every translatable function recompiled loads and then page-faults (read access to 000000B4, or write to 0102519B at an EIP that is in no module); the 156-function set runs fine
tags: pc,recomp,bisect,libigdisplay,tooling
created: 2026-08-05
updated: 2026-08-05
---

## Symptom

`tools/build_recomp.sh ALL` produces a libIGDisplay.dll that loads (the
loaddll trace shows it) and then dies before rendering. The 156-entry-point
set in `scratch/recomp/verified.eps` runs the game to the intro cinematic.

## What was measured

`tools/bisect_recomp.sh` converged in 15 rounds on a single function:

    0x10002c00  Gap::Display::igWindow::_instantiateFromPool   (6 instructions)

**Then the assumption that mattered turned out to be wrong.** Building
all-521-minus-that-function still failed on the real game. One bisection
answers "which function breaks the good set"; it does NOT answer "which set
is the largest that works", because the culprits are independent. The script
now loops -- find a culprit, exclude it, try the rest again -- until what
remains runs.

## What the watch showed about the first culprit

With `X2_WATCH` (I019) on the culprit and the `arkRegister` it calls:

    [WATCH] 0x10002c00 #1  esp=0x00b7fd6c ecx=0x02429ee0 ret=0x005f6c39
                           arg0=0x02429ee0  [0x10021b80]=0x024562c0
    [WATCH] 0x10002c70 #1  esp=0x00b7fd68 ecx=0x02429ee0 ret=0x10002c05
                           arg0=0x005f6c39  [0x10021b80]=0x024562c0

Neither printed a `RETURNED` line, so **the fault is inside the
`igArkRegister` call**, not downstream of it. The page fault's EIP,
0x02429EE0, is exactly `arg0` -- the `igMemoryPool*`. Something transferred
control to the pool pointer.

## Ruled out by measurement, not reasoning

Three readings of the generated C that were WRONG, each killed by the watch or
by arithmetic against the original:

* "The class meta-object global at 0x10021b80 is NULL." It holds 0x024562c0.
* "The ESP arithmetic across `x86_call_host` is off." Traced with the real
  numbers from the watch: entry esp 0x00b7fd6c through PUSH EAX, the thiscall
  `ret 4`, and the final RET lands on exactly the same esp as the original.
* "Immediates holding image addresses are not rebased." They are:
  `PUSH 0x10002c80` emits `WR32(C->esp, (G_IMGBASE + 0x2c80U))`.

Also checked and clean: only ONE export name maps to RVA 0x2c00 (no aliasing),
and IAT slot 0x100091f0 maps to the correct `igArkRegister` overload of the
two the DLL imports.

## Next

The remaining question is what `igArkRegister` does with ECX. `x86_call_host`
sets `%ecx` from `C->ecx` before `call *%[f]` for every import, including
`__cdecl` ones -- here that means the callee is entered with ECX =
0x02429ee0, the value that becomes EIP. Worth disassembling
`x86_call_host` in the built DLL and the original code at RVA 0x2c80 (the
callback handed to igArkRegister).

### Resolution (2026-08-05)
ROOT CAUSE (C080): the entry path kept the runtime's own state BELOW the guest stack pointer, where the guest and every host function it calls write.

Two places, one rule broken:
  * x86_enter's `CPU C` sat 36 bytes below guest_esp (measured by the new
    x86_watch_stack report). libIGCore's igArkRegister does `sub esp,0x108`
    and overlapped 196 of the struct's 232 bytes; the fault EIP was the CPU
    struct's own base address.
  * the export shim's return address sat 20 bytes below the entry ESP, so
    after the last host callee had used that memory the shim returned into it.

Fix: a private per-thread runtime stack (x86_rt_stack_take/give) and
x86_enter_tramp, reached by `jmp` from a shim that pushes nothing that has to
outlive the body. The three scratch words it does put below the entry ESP are
consumed before the body runs.

The bisection this issue describes was therefore looking for the wrong thing:
the culprits were not independent and there was no largest-working-set to find.
Every 'culprit' was simply a function that CALLS A HOST FUNCTION, which is what
triggers the overwrite. tools/build_recomp.sh ALL now renders (2307 colours,
17 recompiled entries over 9 entry points, zero fault blocks).
