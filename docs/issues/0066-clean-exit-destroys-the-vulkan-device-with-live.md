---
id: 66
title: Clean exit destroys the Vulkan device with live swapchain objects
status: resolved
symptom: vkDestroyDevice reports live VkSemaphore objects and vkDestroyInstance reports a live VkSurfaceKHR after the guest releases Direct3D and destroys its window
tags: pc,native,gpu,vulkan,sdl,shutdown,resource-lifetime
created: 2026-08-14
updated: 2026-08-14
---

## Observation

The passing full smoke loop exited code 0 but Vulkan validation reported
`VUID-vkDestroyDevice-device-05137` for at least ten live semaphores and
`VUID-vkDestroyInstance-instance-00629` for the swapchain `VkSurfaceKHR`.
Mesa also reported Wayland queue proxies still attached.

## Root cause

`USER32!DestroyWindow` called `SDL_DestroyWindow` directly while SDL_GPU still
owned the window's swapchain. Later, `gpu_device_destroy` tried to release that
claim through a stale `SDL_Window *`. The host violated SDL_GPU's ownership
order: the swapchain children must be released while both the GPU device and
window still exist.

The fix calls `gpu_device_attach_window(NULL)` before `SDL_DestroyWindow`.
This is the same ordering already used by the isolated GPU self-test.

## Verification

Before the fix, the full-route log contained at least ten live-semaphore VUIDs,
one live-surface VUID, and attached Wayland proxies. After it,
The scripted smoke scenario passed the complete 4,135-frame route. An explicit scan
of all 1,424 log lines / 139,087 bytes found:

    VUID-vkDestroyDevice-device-05137: 0
    VUID-vkDestroyInstance-instance-00629: 0
    Validation Error: 0
    destroyed while proxies still attached: 0

`x2native --no-window --d3d8-selftest` also passes. C177 records the claim and
its falsifier.
