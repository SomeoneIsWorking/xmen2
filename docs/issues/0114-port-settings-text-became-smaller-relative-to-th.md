---
id: 114
title: Port Settings text became smaller relative to the window at higher resolutions
status: resolved
symptom: Increasing the host output resolution made native Port Settings text and controls occupy a smaller fraction of the window even though the retail 800x600 game surface scaled correctly.
tags: resolution,rmlui,settings,presentation
created: 2026-08-24
updated: 2026-08-24
---

## Root cause

The RmlUi context used only SDL display-density scaling. Output pixel density relative to the native UI design space was omitted, so increasing resolution on the same display left one dp equal to the same physical pixel count while the retail 800x600 surface grew through aspect-fit composition.

## Resolution

The UI now derives its resolution scale from the shared 1280x720 design-space aspect-fit calculation and combines it with SDL display scale by taking the larger density. The retail guest remains an 800x600 logical surface; no registry value controls host resolution. The aspect-fit unit test covers 1280x720, 1920x1080, and 3840x2160 proportions. A live 1920x1080 HTTP capture verified the scaled Port Settings composition and readable proportional text.
