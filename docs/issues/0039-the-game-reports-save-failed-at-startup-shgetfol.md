---
id: 39
title: The game reports SAVE FAILED at startup -- SHGetFolderPathA is not implemented, so there is no save folder
status: resolved
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


## Done: the save folder exists, and the game writes to it

`src/native/shell32.c` implements `SHGetFolderPathA` for the four per-user data
CSIDLs and refuses every other one BY NUMBER -- each of those means something
specific, and a program that asked for the Windows directory and got a save
folder would write into it.

The guest is handed a path on a **virtual drive**, `S:`, not a host path:
`win_path()` resolves everything the guest says against `$GAME_PC_DIR`, so a
POSIX path given to the guest comes straight back as the install plus the whole
thing. `win_path` maps that one letter to the save directory instead. The host
directory defaults to `scratch/saves` (`X2_SAVE_DIR` overrides) and is CREATED,
not merely named.

Two more defects fell out of testing it, both real:

* **`CreateFileA` handled ONE of the five dispositions.** Only `CREATE_ALWAYS`
  created a file; `OPEN_ALWAYS` and `CREATE_NEW` -- which is how a first save is
  written -- fell through to "open, do not create" and returned
  ERROR_FILE_NOT_FOUND. All five are handled now, and an unknown one is
  refused rather than guessed: they differ in whether they CREATE and whether
  they TRUNCATE, and picking wrong either loses a file or invents one.
* **A failed open said nothing.** Returning INVALID_HANDLE is the correct Win32
  answer and a terrible diagnostic. It now names the guest path, the host path
  it became and the disposition -- which is what showed the double separator
  below.

`SHGetFolderPathA` returned `"S:\\"` with a trailing separator at first;
Windows returns none (the caller appends its own), and the very first path the
game built was `S:\\\\Activision\\...`.

**Result, verified:** the game creates
`Activision/X-Men Legends 2/{Save,Screenshots}` and writes a 684-byte
`Save/settings.dat`. No `CreateFileA` fails anywhere in the run.

## Resolved: CreateDirectoryA never set ERROR_ALREADY_EXISTS

The last piece was not a missing feature -- it was a missing ERROR CODE.

`CreateDirectory` returns FALSE for a directory that already exists, on Windows
too, and every caller that creates a tree distinguishes that from a real
failure by `GetLastError() == ERROR_ALREADY_EXISTS`. This host returned FALSE
and left the last error at whatever it happened to be, so "the directory is
already there" read as "the directory could not be made" -- and the game
correctly concluded it had nowhere to save, on every run after the first.

    [FILE] CreateDirectory    "S:\Activision\X-Men Legends 2\Save"
             -> "scratch/saves/Activision/X-Men Legends 2/Save"  File exists

One line: `errno == EEXIST` now becomes ERROR_ALREADY_EXISTS (and ENOENT
becomes ERROR_PATH_NOT_FOUND). `DeleteFileA` got the same treatment.

**The dialog is gone** -- `scratch/screenshots/after.png` is the title screen
with its full legal paragraph and no panel over it. The later libIGGfx x87 stop
was independent of this file-error mapping defect.

## What found it

`X2_FILES=1` traces every file operation the guest asks for, with the guest
path, the host path it became and the outcome. There is no `strace` on this
machine, and the question here was not "which open failed" -- every open
SUCCEEDED -- but "what did the game try", which only a trace of the successful
calls can answer.

## Superseded: the wrong anchor

The settings file writes and no file operation fails, and the game STILL shows
its dialog. So the failure it is reporting is not the one that was fixed.

A dead end worth recording, and it stayed a dead end: the exe contains the string `EMSG_SAVE_FAILED_DEVNUM`
at 0x0069ad04, selected by `FUN_0055e9b0`, which is a plain
error-code-to-message-id switch. **That function is never called in a run** (an
entry trace on it reports zero calls), so it is the wrong anchor -- the
displayed text is "Save failed!" with no device number, so the id being looked
up is a different one, and it most likely comes from the localised string table
in `igct.bnx` rather than from the exe.

The next anchor is that string table, not the exe's literals.


Both message keys (`EMSG_SAVE_FAILED_DEVNUM` and `EMSG_CREATE_GAME_FAILED_DEVNUM`,
which are both "Save failed!" in `igct.bnx`) appear ONLY inside `FUN_0055e9b0`,
and that function is never entered -- verified against a positive control in
the same run, so the negative is trustworthy. The dialog is raised from data,
not from an exe literal, which is why chasing the string could not have worked.
The file-operation trace found it in one run.
