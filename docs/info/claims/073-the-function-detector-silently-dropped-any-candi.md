---
id: C073
kind: claim
status: holds
created: 2026-08-05
tags: xbox
---

## Claim

The function detector silently dropped any candidate the linear sweep had desynchronised past. The sweep decodes bytes in order, so an embedded jump table shifts every following boundary until it happens to resync; a real function start inside that shifted region simply has no instruction at its address, decodes to zero instructions, and was skipped by a bare 'continue'. A seeded address therefore looked like a wrong guess, which is what stalled the discovery loop twice on 0x00269CA0. Candidates are now decoded from their own address, and the pass reports its denominator either way.

## Evidence

0x00269CA0 is a clean 0x30-byte thiscall (mov eax,[ecx+8]; movzx ecx,[ecx+0x14]; rep movsd; rep movsb; ret 8) reached by an indirect call at runtime. functions.json had no function there and no neighbouring function contained it -- it sat in the hole 0x00269C89..0x00269CD0. objdump from 0x00269C89 shows why: the five dwords of the jump table at 0x00269C8C decode as instructions and the stream resumes on a 6-byte boundary at 0x00269C9F, so nothing starts at 0x00269CA0. After decode_from: 148 candidates across the title needed decoding from their own address, function count 25781 -> 25929, sub_00269CA0 is in the dispatch table, and the run reaches 19592 indirect calls, up from 13954. The 34 candidates that STILL decode to nothing are now printed by address and are all adjacent-byte vtable-harvest false positives (0x00014184 and 0x00014185, 0x0005A403/5/6/8/9/A/B/D).

## What would falsify it

if any of the 148 re-decoded candidates is NOT a real function, decode_from is manufacturing functions out of data rather than recovering them -- the tell would be new unresolved targets or garbage bodies, not fewer
