---
id: 127
title: Native battery assumes guest allocation size equals request
status: resolved
symptom: native_battery consistently fails delete[] restores heap usage by eight bytes after cutscene overrides change heap fragmentation
tags: native,tests,heap,selftest
created: 2026-08-27
updated: 2026-08-27
---

## Root cause

`check_delete_array` records heap usage only after `guest_malloc(64)` and expects freeing to subtract exactly 64 payload bytes. The allocator deliberately consumes the entire free block when its remainder cannot hold a header plus aligned payload, so the allocated payload may be 64-72 bytes depending on fragmentation. The observed eight-byte difference is a false failure.

## Proper check

Record the heap baseline before allocation and require delete[] to restore that exact baseline. The skipped-body negative control must retain the allocation and still fail.

### Resolution (2026-08-27)
The selftest now captures heap usage before the allocation and requires delete[] to restore that exact baseline. This handles the allocator's documented whole-block consumption when a remainder cannot fit header plus aligned payload; native_battery passes and the skipped-body negative control remains binding.
