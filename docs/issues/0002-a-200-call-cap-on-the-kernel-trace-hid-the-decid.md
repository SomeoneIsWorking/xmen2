---
id: 2
title: A 200-call cap on the kernel trace hid the deciding call
status: resolved
symptom: an ordinal appears never to have been called, but grep only covers the printed trace
tags: xbox,tooling,instrument
created: 2026-08-05
updated: 2026-08-05
---

The kernel trace prints only the first 200 calls, and the run makes 473. Grepping the log for 'ordinal 217' found nothing and I concluded NtQueryVirtualMemory was never called -- it was called, five times, and it was the deciding call in the whole investigation.

The tell was already in the log: '[KERNEL] 473 kernel calls total (the per-call trace above stops at 200)'. The total was honest; the conclusion drawn from the trace was not.

Fixed by capping the BORING case instead: a placed reservation (a non-zero base hint with MEM_RESERVE) is rare and always interesting, so it always prints regardless of call count. The general rule this is an instance of: cap by novelty, not by position.
