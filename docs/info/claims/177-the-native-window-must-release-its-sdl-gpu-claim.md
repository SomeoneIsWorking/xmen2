---
id: C177
kind: claim
status: holds
created: 2026-08-14
tags: gpu,vulkan,sdl,shutdown
---

## Claim

The native window must release its SDL_GPU claim before SDL_DestroyWindow; doing so leaves no Vulkan swapchain children alive at clean game shutdown.

## Evidence

src/native/win32_sdl.c imp_USER32_DestroyWindow detaches through gpu_device_attach_window(NULL) before SDL_DestroyWindow. Before: the captured validation output recorded in issue #66 reported at least ten VUID-vkDestroyDevice-device-05137 semaphore leaks, VUID-vkDestroyInstance-instance-00629 for VkSurfaceKHR, and Wayland proxies still attached. After: the scripted smoke scenario passed the full 4135-frame route; scanning all 1424 lines / 139087 bytes of scratch/logs/smoke_loop.log found 0 device-lifetime VUIDs, 0 instance-lifetime VUIDs, 0 Validation Error lines, and 0 attached-proxy warnings. --d3d8-selftest also passed.

## What would falsify it

A clean game exit after DestroyWindow reports a live Vulkan semaphore/surface, an attached swapchain proxy, or a use-after-free involving the released SDL window.
