---
id: C037
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The Xbox game project is scaffolded and configures on Linux; the remaining build errors are confined to the template's Windows-only main.c (GetCommandLineA, GetModuleHandle, WinMain signature, EXCEPTION_POINTERS shape).

## Evidence

xbox/CMakeLists.txt (from the upstream new-game template, adapted: Windows import libraries made conditional, WIN32 dropped from add_executable, include paths extended to the toolkit's platform dir and the generated gen/ dir) configures with cmake rc=0, finding epoxy. All 7 runtime libraries build. The build now fails with 10 errors, ALL in xbox/src/main.c, which the template writes against the Win32 API.

## What would falsify it

I attempted the main.c port and made it worse -- added an EXCEPTION_RECORD that the header already defined, a CONTEXT that already existed as an x64-shaped struct, and a main() the template already had. Those were reverted and only the four verified portability fixes kept. That sequence of avoidable conflicts is evidence I could no longer hold the header's contents in working memory, so the main.c port should be done fresh rather than continued here.
