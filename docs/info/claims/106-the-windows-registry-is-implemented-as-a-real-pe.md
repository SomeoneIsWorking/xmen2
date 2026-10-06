---
id: C106
kind: claim
status: holds
created: 2026-08-06
tags: pc,jit,native,host,registry
---

## Claim

The Windows registry is implemented as a real persistent store, and the game round-trips 64 settings through it

## Evidence

src/native/advapi32.c implements RegOpenKeyA/ExA, RegCreateKeyA/ExA, RegCloseKey, RegQueryValueExA, RegSetValueExA, RegEnumValueA and RegEnumKeyExA over a text-backed key/value store. A registry IS a persistent key/value store, so this is an implementation rather than a stub -- the tempting shortcut (succeed on write, ERROR_FILE_NOT_FOUND on every read) would let the game start and take its defaults while every setting it saved vanished, which is a bug the player finds rather than the developer. VERIFIED by what the game itself wrote: 64 values persisted in one run under HKCU\\Software\\Activision\\X-Men Legends 2\\Settings, with correct Win32 types -- Resolution as REG_SZ holding the bytes 38303078363030 ('800x600'), FSAA and Version and WarningRes as REG_DWORD. So the engine's igWin32Registry wrapper drives it end to end and reads back what it wrote. A key that was never written returns ERROR_FILE_NOT_FOUND, which is what Windows does and what the engine already treats as 'not configured'. The store lives at  or ./x2registry.txt, deliberately NOT inside the game install, which is never written to. Pairs entered 4172 -> 4189; battery 33/33.

## What would falsify it

Bounded arrays: 512 values, 64 open keys, 512 bytes per value, 256-byte key paths. Each REFUSES loudly rather than truncating -- a truncated setting is worse than a failed write -- but no run has come near those bounds, so 'the game fits' is untested rather than established. RegEnumKeyExA also derives subkey names from value paths, so a key holding no values anywhere beneath it is invisible to enumeration; Windows would list it.
