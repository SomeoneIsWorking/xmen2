---
id: 44
title: The movie player needs GDI: CreateCompatibleDC and the bitmap blit path
status: open
symptom: x86_missing_import: GDI32.dll!CreateCompatibleDC, reached once libCriMovie's decoding thread is running
tags: pc,native,gdi32,movie,libCriMovie
created: 2026-08-07
updated: 2026-08-07
---

## Where

Only reachable since guest threads landed (issue #43): libCriMovie's decoder
thread runs, and the movie player then asks GDI for a memory device context --
the classic Win32 way to hold a DIB and blit frames into a window.

## What it means

This is a rendering path that predates the engine's own renderer: the movie
player draws through GDI, not through Direct3D. So the surface it wants is a
host bitmap, and the frames end up in a window this port draws with SDL.

The shape of the answer, before any code:

* `CreateCompatibleDC`/`DeleteDC`, `CreateDIBSection` or `CreateCompatibleBitmap`,
  `SelectObject`, `BitBlt`/`StretchBlt`. Read what libCriMovie's IAT actually
  imports before implementing any of it -- the set is small and knowing it
  exactly is the difference between a subsystem and a guess.
* A DC here is a handle to a host-side bitmap: width, height, format, pixels.
  Blitting it to the screen means uploading it as a texture and drawing a quad,
  which `src/gpu` already does for everything else.

## Worth deciding first

The intro FMV is not the game, and this is the second subsystem it has pulled
in (threads, now GDI). Implementing GDI to play a logo movie is a legitimate
choice and so is declining the movie at the point the engine REQUESTS it -- but
that decision should be made deliberately rather than by following the imports
one at a time. See issue #43's option B.
