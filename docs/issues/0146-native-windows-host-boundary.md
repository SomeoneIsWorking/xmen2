# Native Windows release host is not implemented

## Finding

The X-Men release workflow deliberately publishes Linux, macOS, Android, and
WASM artifacts but has no Windows package. Its Windows CI job is policy-only:
`tools/ci.py` rejects `native-components` and `release_binary` for
`windows-x86_64` because the native host boundary is absent.

## Cause

The shipping host currently depends on POSIX-only owners for executable memory
and cache protection (`sys/mman.h`), file and directory operations, sockets,
threading, signals, `/proc` diagnostics, and `dlopen`. CMake also links POSIX
libraries directly in tests and runtime targets. The x86port AArch64 paths do
not provide a Windows host ABI/executable-memory implementation. Adding a
Windows workflow or ZIP without these owners would produce a policy-only or
non-runnable artifact, not a release.

## Required work

Port the shared executable-memory/cache and host ABI boundaries first, then
replace title-side POSIX file, thread, socket, signal, and diagnostic owners
with Windows implementations. Add a real Windows runner that configures,
builds, tests, packages, and launches a synthetic asset-free runtime before
adding the artifact to the release workflow.

## Current evidence

The Windows policy job passes only the explicit unsupported-target refusal.
Linux AppImage, macOS `.app`, Android arm64-v8a, and Pages artifacts are
independently built and verified. The falsifier is a Windows runner producing a
signed or explicitly portable ZIP whose native binary passes the same runtime
boundary and package checks.

The first title-side boundaries are now isolated in
`src/native/platform_mman.h` and `src/native/platform_file_map.{c,h}`:
guest-memory mapping, protection changes, page-size discovery, release, and
read-only PE file mapping all have one platform owner. Their Windows branches
use `VirtualAlloc`/`VirtualProtect`/`VirtualFree` and
`CreateFile`/`MapViewOfFile`; the Linux paths keep the existing POSIX
contracts. The focused guest-memory suite passes all seven mapping/protection
cases, the file-map regression passes, and the PE loader now consumes the
shared map. This is a portability step, not a Windows build claim: threads,
sockets, signals, diagnostics, CMake dependency links, and the Windows
dependency/toolchain job remain open.

The native sources also now consume one `platform_strings.h` compatibility
owner for case-insensitive comparisons, mapping to `_stricmp`/`_strnicmp` on
Windows and the existing POSIX functions elsewhere. This removes the direct
`strings.h` header blocker without changing comparison semantics; the native
Linux target still compiles and links after the change.

Synchronization and timing now have the same boundary in
`src/native/platform_threads.h`. Windows uses SRW locks, condition variables,
`CreateThread`, high-resolution clocks, `Sleep`, and `SwitchToThread`, while
POSIX builds retain `pthread`/`clock_gettime`/`nanosleep`. The native target,
guest-memory tests, and guest-call-stack thread test pass on Linux; a Clang
Windows-target syntax pass accepts the new Windows branch with Zig's SDK
headers. This still does not prove a linked Windows executable because the
remaining POSIX file, socket, signal, and CMake dependency owners are open.

The remaining direct `unistd.h` includes in title/test sources now route
through `platform_posix.h`, which maps the required CRT operations on Windows
(`_read`, `_write`, `_close`, `_mkdir`, `_fullpath`, `_pipe`, and related file
offset calls) while retaining the POSIX names on Linux. The full native Linux
build and 149-test CTest run pass, with only the documented data/tool skips;
this further narrows the Windows work to real host semantics rather than
header portability.

The live control channel now has the same narrow socket owner in
`src/native/platform_socket.h`. It centralizes Winsock startup, descriptor
width, accept/send/receive/close, loopback bind/listen, and interrupt handling;
route owners no longer expose POSIX `int` descriptors. The Linux control,
screenshot, and `x2ctl` tests pass after this boundary change. A Windows
package is still not claimed: the socket header needs to be compiled and
linked as part of the remaining native host port, alongside signals,
diagnostics, directory/stat operations, `dlopen`, and the CMake dependency
selection.

Directory enumeration now has one portable owner in
`src/native/platform_dirent.h`. It maps `DIR`, `struct dirent`, `opendir`,
`readdir`, `closedir`, and `rewinddir` across POSIX and Windows
(`FindFirstFileA`/`FindNextFileA`), routing `kernel32`, `win_path`, and
`save_catalog` away from raw `<dirent.h>`. The unused `<sys/wait.h>` include
in `x2native.c` has also been removed. The complete native test suite
(149 tests) continues to pass cleanly with all checks green.
