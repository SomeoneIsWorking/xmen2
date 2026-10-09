---
id: 195
title: src/native holds half the codebase with no single owner
status: open
symptom: src/native has 463 files (62k lines) mixing Win32 emulation, the engine, the control channel, game features and probes; several rules are copied across files
state_items:
tags: structure,refactor
created: 2026-10-09
updated: 2026-10-09
---

# 0195 — src/native holds half the codebase with no single owner

## Duplicated rules

Each collapses into one owner before any file moves:

| Rule | Copies | Owner |
|---|---|---|
| Guest argument `A(i)`, `ret_std`, `ret_com` | 13 / 6 / 4 | `src/native/stdcall_import.h` |
| `exe_base()`, `mapped_exe_base()`, `EXE_PREFERRED` | 13 / 2 / 12 | `guest_modules.h` over `x86_module_base` |
| `guest_call0`, `thiscall` | 4 / 3 | `guest_call.hpp` |
| `guest_ptr` | 3 (d3d8) | `d3d8_com.h` |
| verify/override enable gates | 7 | one header beside the overrides |
| `now_s` wrappers of `guest_clock_now_s` | 2 | call it directly |
| `kind_name` (dinput) | 2 | `dinput_device.h` |

## Directory split

`src/native` splits into owners, reusing existing directories where one fits:
`platform`, `runtime` (guest address space, PE map, clock, paths, import
registry), `engine` (JIT glue, dispatch, guest threads/heap/TEB), `win32`,
`user32`, `crt`, `dinput`, `ig_overrides`, `cutscene`, `menu`, `boot`,
`prompts`, `hud`, `lan`, `control`, `probes`, `setup`, `app`, plus files that
join `diagnostics`, `config`, `net`, `audio`, `media`, `d3d8`, `presentation`,
`save` and `input`.

Layer order (a directory includes only lower tiers): platform; diagnostics,
config, net; runtime; engine; win32, crt, d3d8, gpu, vulkan, audio; media,
presentation, input, save; dinput, user32, ui, boot, menu, cutscene, prompts,
hud, ig_overrides; lan; control, probes, setup; app, web. Existing upward edges
(for example `win32_sdl.h` used by audio and d3d8, `lan_session.h` used by
boot and save) are seams to cut, tracked by a layer checker as a ratchet.

Each move is one commit that builds on Linux, Windows and Android, with
includes written as `"dir/file.h"` from `src/`.
