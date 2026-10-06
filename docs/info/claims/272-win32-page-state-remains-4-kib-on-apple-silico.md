---
id: C272
kind: claim
status: holds
created: 2026-08-26
tags: pc,native,macos,arm64,memory,kernel32
depends: src/native/guest_memory.c#apply_host_protection, src/native/x2native.c#case_guest_page_granularity
---

## Claim

Win32 page state remains 4 KiB on Apple Silicon; 16 KiB is only the final host protection granule, and decommitting one logical page cannot revoke a committed sibling

## Evidence

Issue #123's trace commits `0x068b9000`, `0x068ba000`, and `0x068bb000`, then decommits only the range beginning at `0x068bb000`; the old 16 KiB bookkeeping faults at the live `0x068b9004`. The 93-check arm64 battery recreates two mixed-state 4 KiB siblings in one host granule and reads the committed one successfully. The same driven run then passes `i105.sfd`, enters the playable world and continues world/shadow geometry submission for another 50 seconds.

## What would falsify it

A logical 4 KiB page changing state when only a sibling is committed/decommitted, `VirtualQuery` reporting the host-granule union instead of the requested page's state, or the post-`i105.sfd` arena fault recurring at an address outside the explicitly decommitted range.
