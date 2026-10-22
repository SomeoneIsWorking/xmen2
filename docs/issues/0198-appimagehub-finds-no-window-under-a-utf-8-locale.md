---
id: 198
title: AppImageHub finds no window under a UTF-8 locale
status: resolved
symptom: "AppImageHub test: 'The application did not open a window within 30 seconds', although the setup prompt is on screen"
state_items: S017
tags: appimage,release,sdl,x11,setup
created: 2026-10-22
updated: 2026-10-22
---

# 0198 — AppImageHub finds no window under a UTF-8 locale

## Reproduction

Ubuntu 22.04, `Xvfb :99 -screen 0 800x600x24`, `LANG=C.UTF-8`, no saved
install: start the AppImage, then `xdotool search --onlyvisible --name '.'`.
Observed: no match, while `xwininfo` shows the prompt IsViewable. Expected: the
prompt. With the C locale it matches in 0.5 s.

## Root cause

SDL 3.4.16 `SDL_x11window.c:SDL_X11_SetWindowTitle` converts the title with
`XTextStyle`, so under a UTF-8 locale `WM_NAME` gets type `UTF8_STRING`, which
xdotool's name match cannot read. ICCCM `WM_NAME` is `STRING` or
`COMPOUND_TEXT`. Upstream main still has it.

## Resolution

Fork branch `xmen2/release-3.4.16` of SomeoneIsWorking/SDL is release-3.4.16
plus `XStdICCTextStyle` there; `tools/dependency_archives.py` pins that commit.
The SDL message-box prompt it found was itself the wrong screen: desktop setup
is now the shared `setup-ui` RmlUi screen (`src/native/install_setup.cpp`),
which setup-ui sizes to the display's usable bounds.

Live: on a GitHub ubuntu-22.04 runner, `firejail --noprofile --net=none
--appimage`, the prompt was found after 2 s with `WM_NAME(STRING)`.
