---
id: 39
title: The game reports SAVE FAILED at startup -- SHGetFolderPathA is not implemented, so there is no save folder
status: open
symptom: The first thing the native run draws is the game's own dialog reading 'SAVE FAILED!' with [Esc] CANCEL / [Enter] RETRY; the log shows GetProcAddress(shell32.dll, "SHGetFolderPathA") -> NULL twice just before it
tags: pc,native,kernel32,shell32,saves
created: 2026-08-07
updated: 2026-08-07
---

## What it is

Not a rendering bug -- the renderer is drawing the game's real dialog, and
correctly (C143). The game asks Windows where the user's documents folder is,
gets nothing, and truthfully reports that it cannot save.

    kernel32: GetProcAddress(shell32.dll, "SHGetFolderPathA") -- this host does
    not implement that entry point, so NULL

Twice, immediately before the dialog.

## What implementing it means

`SHGetFolderPathA(hwnd, csidl, hToken, dwFlags, pszPath)` fills a MAX_PATH
buffer. The two that matter are `CSIDL_PERSONAL` (5, My Documents) and
`CSIDL_APPDATA` (26). This port's answer should be a directory it OWNS rather
than a guess at the user's home: the project's own rule is that the install is
read-only and run state lives under the repo, so a save folder under
`scratch/` (or an explicit env override) is the honest mapping, and it has to
be CREATED, not just named -- returning a path the game then fails to open
moves the same failure one step later.

The dialog offers RETRY, so the game keeps running either way; this is not a
blocker, it is the first thing a player sees.
