---
id: 138
title: Android setup stages the install on the UI thread and dies mid-copy
status: resolved
symptom: Black screen forever after choosing the install folder in the Android setup Activity; the task is then killed
tags: android,setup,saf,staging,ui-thread
created: 2026-08-30
updated: 2026-08-31
---

Observed on an Honor VKJ-NX9 (Android 16, arm64-v8a) with APK 0.2.0 on 2026-08-30.

## Symptom

`XMen2SetupActivity` showed its Browse screen and the SAF picker worked, but
after confirming a 2.2 GB install folder the screen went black and stayed
black. logcat then reported, in order:

    ActivityTaskManager: Activity top resumed state loss timeout for ... XMen2SetupActivity
    ActivityTaskManager: Activity stop timeout for ... XMen2SetupActivity
    ActivityTaskManager: Destroy timeout of remove-task, attempt to kill Task ...
    ActivityManager: Killing 15307:com.someoneisworking.xmen2 (adj 905): remove task

`dumpsys activity exit-info` recorded reason=10 (USER REQUESTED) subreason=22
(REMOVE TASK). The process was sleeping in state S the whole time while its
CPU ticks climbed steadily, i.e. it was doing the copy, not deadlocked.

## Root cause

`XMen2SetupActivity.handleFolder()` called `copyTree()` directly from
`onActivityResult`, so the entire recursive SAF copy -- thousands of
content-provider round trips over gigabytes -- ran on the main thread. The
Activity could not draw or answer lifecycle callbacks for the duration, so the
system timed out its stop and removed the task, killing the copy partway
through. The ZIP branch had the same defect.

Two further defects were found in the same code:

- `resumeSavedInstall()` accepted any recorded path that merely `exists()`, so
  a half-copied tree left by an interrupted run would be resumed as a valid
  install.
- The picker asked for `XMen2.exe` with `ACTION_OPEN_DOCUMENT` and then, on
  success, immediately asked *again* with `ACTION_OPEN_DOCUMENT_TREE`. A
  single-document URI grants access to one file and never its siblings, so the
  executable step could not contribute anything; only the folder step ever
  produced an install.

## Fix

- New `InstallStaging` owns traversal, copying, progress, cancellation, and
  commit on its own thread. The Activity keeps only UI and picker wiring.
- Determinate progress from a `COLUMN_SIZE` pre-pass, plus a Cancel button and
  Back handling.
- A partial wake lock and `FLAG_KEEP_SCREEN_ON` for the duration, because the
  OEM power manager had already been observed freezing this app about a second
  after it lost foreground.
- Staging writes to `filesDir/staging` and commits by renaming onto
  `filesDir/install`, so an interrupted run leaves nothing a later launch can
  mistake for a complete install. `InstallStaging.committedSource()` is now the
  only resume authority.
- Browse opens the folder picker directly; ZIP is a separate explicit button.

## Evidence

`tests/test_android_release.py` pins the contract: the setup must not perform
provider I/O on its thread, must not branch on an `.exe` selection, must hold
`WAKE_LOCK`, and staging must run on its own thread.

### Note (2026-08-31)
Superseded during the same session: the staging copy was removed entirely rather than moved off the UI thread.

Measured on the Honor VKJ-NX9, the SAF copy ran at 0.05 MB/s across the game's many small files -- per-file content-provider latency, not bandwidth -- so the background-thread version would still have taken ~12 minutes and still have duplicated 2.2 GB on the device. The port now requests MANAGE_EXTERNAL_STORAGE and reads the install in place; InstallStaging.java is deleted and InstallLocation.java resolves a picked document to its filesystem path, refusing a provider that has none.

Two further launch blockers were found behind it, both invisible until native stdio was routed to logcat (SDL3 does not redirect it, so every fatal refusal printed into the void):

- The manifest never declared INTERNET, so socket() failed with EACCES and control_start() exit(2)'d before the game ran. This presented as 'FORTIFY: pthread_mutex_lock called on a destroyed mutex' in hwuiTask1, which is only the Android render thread touching a mutex that exit()'s static destructors had already torn down -- a secondary effect, not the cause.
- guest_memory mapped the guest 32-bit space 1:1 at low host addresses, which Android's loader and ART already occupy, so the 512 MB guest heap arena at 0x71000000 could not be placed. The pre-reserved rebased arena that already existed for Apple arm64 is the same capability, so its guard is now GUEST_ARENA_RESERVED rather than a vendor check. Device page size is 4096, so HOST_PAGE_SIZE stays a separate Apple-only concern.

Packaged run artifacts (input recordings, live-session record) also moved from a cwd-relative scratch/ path to the OS user-data directory, which is what made them writable on Android.

The game now boots on device and renders the intro scene. Remaining: the surface comes up letterboxed in portrait rather than fullscreen landscape, and touch controls are not yet driving the pad.
