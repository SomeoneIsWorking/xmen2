# Native Windows host: built and booting under Wine, not yet a release

## Finding

`x2native.exe` cross-compiles for Windows x86-64 with llvm-mingw and boots the
retail game under Wine. A portable ZIP is built by `tools/package_windows.py`; no run on real
Windows has been made.

## Host boundary

Every host-dependent owner has a Windows implementation in the `x2_platform`
library, which links `ws2_32` and `iphlpapi`:

| Concern | Owner | Windows implementation |
|---|---|---|
| Guest arena, protection, page size | `platform_mman.h`, `platform_mman_win32.cpp` | `VirtualAlloc`/`VirtualProtect`/`VirtualFree` |
| PE file map | `platform_file_map.{cpp,h}` | `CreateFile`/`MapViewOfFile` |
| Threads, clocks, sleeps | `platform_threads.h` | SRW locks, condition variables, `CreateThread` |
| CRT file calls, `realpath`, `getcwd`, atomic replace | `platform_posix.h`, `platform_posix_win32.cpp` | `_fullpath` with `/` separators, `MoveFileExA(REPLACE_EXISTING)` |
| Directory listing | `platform_dirent.h` | `FindFirstFileA`/`FindNextFileA` |
| Sockets | `platform_socket.h`, `winsock_host.{h,cpp}`, `winsock_resolve.cpp` | Winsock, `WSAPoll`, `GetAdaptersAddresses` |
| Faults | `fault_report.cpp`, `fault_signals_win32.cpp` | vectored exception handler |
| Host code symbols | `src/diagnostics/host_code_location.cpp` | `GetModuleHandleExA` in place of `dladdr` |
| Save timestamps | `save_catalog.cpp` | `GetFileAttributesExA` (100 ns resolution) |

Guest Winsock handles index a slot table (1..1023) over host sockets, so the
`ws2_32` thunks never see a host descriptor.

## Toolchain

The toolchain is llvm-mingw (UCRT, static), not MSVC or clang-cl. This means one
Clang for Linux, the Windows cross build and the Windows runner, and the
JIT-common and SDL3 static libraries build unchanged. `tools/windows_deps.py`
provisions the checksummed toolchain plus zlib, SDL3, SDL3_image, FreeType and
FFmpeg into `build/deps/windows/x86_64`. `tools/build_windows.py` configures
with `cmake/toolchains/llvm-mingw-x86_64.cmake`.

## Evidence (Wine 11 staging, Linux host)

- `ninja -k 0` in a cross build directory builds `x2native.exe` (PE32+ x86-64)
  and every test with no warnings.
- Under Wine, `x2native.exe --fault-selftest` reports all five fault kinds.
  `--selftest` against the retail install fails 0 of 92 checks.
- The boot `--no-window --d3d8 --control=<port>` reaches `/status`. With
  jit-common `e28ccdf` (dual-mapped code section) it presents 17 frames by 5 s
  and 2027 by 60 s on a warm prefix; the whole-region `VirtualProtect` build
  presented 0 and 1260. `perf` of the boot is now flat: no `mprotect_range`.
- An `abort()` writes the `*** CRASH SIGABRT` record (`run_log` under Wine).
- The `windows-x86_64` job in `asset-free.yml` passes on a Windows runner
  (run `37489972297`). The UCRT's `abort()` there fast-fails with
  `0xC0000409` where Wine exits 3; the tests accept both.
- CTest with `CMAKE_CROSSCOMPILING_EMULATOR=wine` passes every host-boundary
  test. Three tests fail because of the cross host, not the product:
  - `control_png` calls the Linux Python.
  - `prompt_glyph_atlas` runs a Windows build tool on the Linux host.
  - `web_touch_play` needs SDL video, which headless Wine does not have.

## Package

`tools/package_windows.py` stages `X-Men Legends II.exe` (the Release
`x2native.exe`), `ui/` (the `tools/ui_resource_files.py` list) and the
`xmen2-package.txt` marker, and zips them as
`X-Men-Legends-II-windows-x86_64.zip`. `src/native/windows_package.cpp` reads
the marker at launch: the compile-time UI path names the build tree, so the
packaged executable publishes the `ui` folder beside itself and takes the
first-run picker route. The import table is 27 system libraries (KERNEL32,
USER32, GDI32, ADVAPI32, SHELL32, WS2_32, IPHLPAPI, WINMM, ole32, SETUPAPI,
the `api-ms-win-crt-*` Universal CRT, ...); the packager refuses anything else.
Every one is needed by SDL3, the guest import layer or the CRT.

Observed under Wine 11 from the unzipped folder alone: `--fault-selftest`
passes, `--selftest` fails 0 of 92, a headless `--d3d8` boot presents 1443
frames by 47 s, and with no install configured the picker dialog comes up.
The `build-windows` job in `release.yml` repeats the unzip and
`--fault-selftest` on `windows-2025`.

## Open work

1. **A native Windows run of the game.** Nothing has been tested on real
   Windows: the window path, input, audio and JIT speed.
2. **CI.** The `build-windows` job has not run.

`x2native.exe` is GUI-subsystem (`Subsystem 2`) in every Windows build, so the
binary CI tests is the one that ships and a double-click opens no console.
mingw's startup still calls `main`, so no `SDL_main` entry is needed. The
selftest and fault-selftest output reaches a redirect or pipe and the Lucent
run log; run from a bare terminal it prints nothing, so redirect it.
