---
id: 72
title: SDL3 GPU context is lost near frame 1920 during menu navigation
status: resolved
symptom: Native SDL3/Vulkan run reports VK_ERROR_DEVICE_LOST after 1919 successful presents and 2034 draws; first loss occurs before the scripted Return action, then texture creation errors cascade
tags: pc,native,gpu,sdl3,vulkan,device-lost
created: 2026-08-14
updated: 2026-08-14
---

## Root cause

The port did not fault the GPU. Kernel records for the observation window name
three separate `gears1` processes as the clients that issued the invalid GPU
accesses (AMD `TCP` permission faults). The third fault caused a device-wide
reset and VRAM loss at 14:13:18--14:13:23. RADV consequently cancelled the
port's context and reported both `VK_ERROR_DEVICE_LOST` and "This context is
innocent." KWin and Plasma then faulted as their own stale contexts encountered
the same reset.

The native probe ran from 14:11:51 through 14:13:51. Its first device-lost
report follows 1,919 successful presents and coincides with that device-wide
reset. It occurs before the input script injects Return, so the controller menu
navigation is not a causal boundary.

## What was tried / dead ends

- Treating frame 1,920 or the fourth Down input as a renderer defect was ruled
  out by the kernel's process/PASID attribution. Frame count was merely when an
  unrelated GPU stress process triggered the global reset.
- The captured frame is all black because capture happened after VRAM was lost;
  it cannot describe the menu state before the reset.

## Resolution

Kernel process/PASID attribution identifies `gears1` as the faulting client;
the global AMD GPU reset invalidated x2native, KWin, and Plasma contexts. No
port code changed. A future device loss is a port defect only if the kernel
attributes the first page fault or ring timeout to `x2native`, or if it
reproduces without an external reset in the same interval.
