---
id: C101
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,translator,diagnostics
reconfirmed: 2026-08-06
---

## Claim

A third linked-vs-mapped confusion: x86_call_unknown named the wrong module, and the discovery loop then fixed a module that was not broken

## Evidence

The emitted call for a direct branch to an unidentified address passed a raw LINKED address: x86_call_unknown(C, 0x1000e070). Every libIG*.dll is linked for 0x10000000, so the runtime resolved it against whichever module occupies that range and reported 'address is in libCriMovie.dll'. The caller was actually in libIGGui. tools/native_discover.sh then did everything right against the wrong target: it seeded libCriMovie, Ghidra reported 'already inside a function', the loop escalated to a SPLIT, the split SUCCEEDED ('created a function at 0x1000e070'), the JSON gained the function, the emit picked it up -- and the run failed identically, because none of it touched libIGGui. Fixed by emitting img_rel() at all three x86_call_unknown sites so the runtime receives a mapped address; it now names libIGGui. This is the THIRD instance of this class, after the boundary ring (I026) and the CALL return address (C093), which is the falsifier C093 named: it is a pattern, not three incidents. Also fixed alongside: x86_call_unknown did not print the seed line shape that native_discover.sh parses, so the loop was blind to this reporter entirely -- it understood the dispatch-target and constructor-target reports and not this one, which makes a stop read as 'nothing more to discover' when it is not.

## What would falsify it

Every emitted site that passes an address to the RUNTIME should now be checked for this, not just the ones a failure has pointed at. x86_untranslated, x86_fallthrough and x86_unsupported_insn all take an ep and have not been audited -- if any of them also passes a linked address, its reports name the wrong module too and nothing has noticed yet.

## Re-confirmed 2026-08-06

Falsifier acted on: the three unaudited reporters were checked and TWO had the same defect. x86_fallthrough and x86_int3 both call where(), which resolves against the module table, and both were emitted with LINKED addresses -- so both would have named the wrong module, exactly as x86_call_unknown did, with nothing to show it. Both now receive mapped addresses. The other two, x86_untranslated and x86_unsupported_insn, only PRINT their address; that is deliberately the guest (linked) address because it is what a reader pastes into Ghidra, and they now say 'guest' rather than leaving the space ambiguous. where() also now prints the mapped address alongside the guest one, so a report cannot be read in the wrong space.
