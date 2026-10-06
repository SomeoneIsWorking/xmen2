---
id: 108
title: RmlUi device assignment incorrectly implied retail co-op participation
status: resolved
symptom: Assigning an input device could make a player appear configured without an explicit join policy, and the pause Players page could drift from host assumptions.
tags: input,co-op,players,rmlui,participation,hotswap,retail
created: 2026-08-22
updated: 2026-08-22
---

Root cause: the host had only a binding publisher and treated device ownership as the entire player model. XMen2.exe has a separate participation singleton returned by 0x0048de40; the pause Players handler 0x005cdb50 queries and transitions that owner through vslots +0x10/+0x14/+0x18/+0x68.

Resolution: device assignment now defines eligibility only. P1 is default-active while eligible; P2-P4 join on a rising Start/Pause action from their exact assigned source; None requests leave. Because the retail Players page is also a writer, every safe pump additionally queries retail participation and removes `active & ~eligible` through the same leave/reconcile API. The native bridge invokes only the retail API, so the pause page reads the same authority. Persistent policy permits dual-device hotswap only for P1. Session-only pads use a nonserialized immutable-GUID owner and remain unresolved rather than roaming after disconnect.

Evidence and blind spot: docs/RE/co_op_participation.md records the static executable/menu evidence. test_settings, test_player_input, test_player_participation_policy, and test_transient_controller_assignment exercise production seams. A bounded synthetic-pad run observed retail masks `0x1` before/after P2 assignment, `0x3` after that assigned pad's Start, and `0x1` after clear; the opened pause Players page showed the same P1/P2 active set. A second live falsifier toggled None-assigned P2 through retail Players; the next pump logged host leave `0x02` and `/input` read P2 inactive again. No physical pad event node exists on this machine, so physical reconnect remains unverified.
