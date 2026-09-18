# 0156 — every guest open enumerated every directory on its path

- **State items:** S021
- **Status:** fixed in `src/native/host_dir_cache.c`

## What it was

The guest asks for Windows paths and the host filesystem is case-sensitive, so
`resolve_case_insensitive` in `src/native/win_path.c` resolved each path
component by listing its directory and comparing case-insensitively. One
`opendir` plus a full `readdir` **per component, per open**, with nothing
remembered between calls.

On a desktop that is a few cached `getdents` calls and invisible. In the
browser it is not: WASMFS runs the filesystem on another thread, so each
enumeration is a cross-thread round trip and the caller sits in
`emscripten_futex_wait` for all of them. Measured in the browser during the
Dead Zone route, with the port's own armed `X2_HOTEP` probe:

```
[HB] top imports by TIME:
  MSVCRT.dll!fopen: 513.4 ms in 22 call(s)
  IDirect3DDevice8!BeginScene: 156.6 ms in 10 call(s)
  MSVCRT.dll!fread: 130.7 ms in 101 call(s)
[HB] wall-time split this interval: host imports 892.1 ms (52%), guest bodies 809.4 ms (48%)
```

23 ms to open one file, and opening files was over half the interval. The cost
was never the comparing; it was doing the enumeration at all.

## The fix

`src/native/host_dir_cache.{c,h}` owns one listing per directory: a 256-bucket
chained table keyed by directory path, each listing one block of names plus an
offset array, so a listing is two allocations and forgetting one is two frees.
`win_path.c` looks names up there instead of enumerating.

A cached listing is only correct while the directory's names are unchanged, and
this cache cannot see changes made behind its back, so the contract is on the
callers: every place in the port that creates, removes or renames a name calls
`host_dir_forget_for` with the path it touched. Those are `_mkdir` and a
write-mode `fopen` in `crt.c`; `CreateFileA` with a creating disposition,
`DeleteFileA`, `CreateDirectoryA` and `RemoveDirectoryA` in `kernel32.c`; the
tree maker in `shell32.c`; and the save rename in `live_session.c`.

Deliberately not time-based and it does not re-stat. A stale entry would
resolve a real file to a name that no longer exists, which is worse than the
cost it saves.

`enumerate` returns NULL rather than an empty listing when `opendir` fails, and
caches nothing then: "this directory cannot be read" and "this directory is
empty" are different answers, and conflating them would resolve a real path to
nothing.

## Measured, same route, same page command line

| | before | after |
|---|---|---|
| `MSVCRT.dll!fopen` | 513.4 ms in 22 call(s) — **23.3 ms each** | 58.1 ms in 56 call(s) — **1.04 ms each** |
| `fopen` in the top five imports | first, every interval | only while loading; absent in gameplay |
| wall-time split | host imports 52%, guest bodies 48% | host imports 16%, guest bodies 84% |

The after figures are from the same run as #155's fix, so the flood is not
inflating either side.

## The test

`tests/test_win_path.c` covers both halves, because a timing cannot tell a
cache that works from one that re-lists every time:

- repeating a resolve must not enumerate again — asserted against
  `host_dir_cache_stats`, not against the clock;
- a file created behind the cache's back must **not** resolve, and must resolve
  after `host_dir_forget_for` — which is what makes the invalidation contract a
  requirement on callers rather than a comment in a header.

Both were checked by breaking the production code: with `host_dir_forget_for`
made a no-op the test fails, with the lookup forced to re-enumerate it fails,
and restored it passes.

## What this did NOT fix

Frame time. The browser route still runs at about 2.2 presents per second. What
bounds it is now guest execution — 84% of the interval in guest bodies, with
283 million block entries over 300 s against 347,371 translated blocks and
`0 of 276,933 condition(s) lowered inline` on the WASM backend. That is the
next frontier, and it is a JIT question, not a filesystem one.
