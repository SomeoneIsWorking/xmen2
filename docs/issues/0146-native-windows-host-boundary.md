# Native Windows host: built and booting under Wine, not yet a release

## Finding

`x2native.exe` cross-compiles for Windows x86-64 with llvm-mingw and boots the
retail game under Wine. There is still no Windows package. The JIT runs about
12x slower under Wine than on Linux, and no run on real Windows has been made.

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
- The boot `--no-window --d3d8 --control=<port>` reaches `/status`. The renderer
  is ready on Vulkan through winevulkan by 15 s. It has presented 1 frame at
  25 s and 702 at 60 s. Linux presents 85 frames by 5 s and 15108 by 60 s.
- CTest with `CMAKE_CROSSCOMPILING_EMULATOR=wine` passes every host-boundary
  test. Three tests fail because of the cross host, not the product:
  - `control_png` calls the Linux Python.
  - `prompt_glyph_atlas` runs a Windows build tool on the Linux host.
  - `web_touch_play` needs SDL video, which headless Wine does not have.

## Open work

1. **JIT throughput.** In jit-common, `jc_code_publish`/`jc_code_begin_write`
   on Windows call `VirtualProtect` over the whole 64 MiB code region on every
   publish. `perf` puts 78.6% of the boot in Wine's `mprotect_range`; the JIT
   ran 5.8M blocks in 5 s, against 71M on Linux. The fix belongs in jit-common:
   flip only the written range, or dual-map the region through
   `CreateFileMapping` with an RX and an RW view, as the POSIX dual-mapped memfd does.
   It has to land in the jit-common dev checkout and then be pinned here.
2. **Windows CI.** The `windows-x86_64` job in `asset-free.yml` runs
   `tools/ci.py native-components --target windows-x86_64`. It installs MSYS2
   `make`, `pkgconf` and `shaderc`, builds FFmpeg with MSYS2's `sh`, and runs
   the host-boundary tests natively. actionlint passes it; it has not run on a
   runner yet.
3. **A native Windows run.** Nothing has been tested on a real Windows host:
   the window path, input, audio and the speed of `VirtualProtect` on real
   Windows.
4. **Package.** After 1 to 3, add a portable ZIP to `release.yml` and record
   the release in S022.

The falsifier is a Windows runner that builds the ZIP and whose `x2native.exe`
passes the runtime-boundary and package checks.
