---
id: C101
kind: claim
status: holds
created: 2026-08-06
tags: pc,jit,pe,diagnostics
reconfirmed: 2026-08-06
---

## Claim

Runtime diagnostics must distinguish a module's linked guest address from its
mapped guest address or they can name the wrong DLL.

## Evidence

Every `libIG*.dll` is linked at 0x10000000, while `pe_map` relocates all but one
at runtime. A diagnostic once received linked address 0x1000e070 from a
libIGGui caller, resolved it against the module occupying that mapped range,
and incorrectly named libCriMovie. Runtime ownership and fault reports now use
the mapped address for module lookup and print the corresponding linked guest
address separately for binary analysis.

## What would falsify it

A relocation run where a fault in one DLL is attributed to another DLL, or a
report that labels an address without naming whether it is linked or mapped,
would invalidate this contract.

## Re-confirmed 2026-08-06

The fallthrough and INT3 reporters were checked against relocated modules and
changed to resolve mapped addresses. Unsupported-instruction reporting retains
the linked guest address for binary analysis and labels it explicitly; module
lookup also prints the mapped address.
