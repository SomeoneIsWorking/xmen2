# Patches against `vendor/xboxrecomp`

`vendor/` is gitignored (it holds the toolkit clone and the copyrighted XBE),
so the toolkit changes this project depends on live here. Apply them to a
fresh clone in order:

```sh
cd vendor/xboxrecomp
for p in ../../patches/xboxrecomp/0*.patch; do git apply "$p"; done
```

Each patch owns a disjoint set of files, so the order only matters for
readability:

| patch | files | what |
|---|---|---|
| `0001-linux-portability` | `src/kernel/kernel_rtl.c` | `wcslen` on a 16-bit Xbox `WCHAR` walks off the end under glibc's 32-bit `wchar_t` |
| `0002-kernel-handle-and-ordinal-tables` | `src/kernel/kernel_bridge.c`, `kernel_file.c` | `bridge_read_handle` indirection; 45 ordinals bound to the wrong function across three tables; FATX volume geometry; loud `HalReturnToFirmware`; file-open logging |
| `0003-recompiler-jump-tables-and-loud-drops` | `tools/` | embedded jump tables decoded as code; function boundaries ending at the last fall-through; deferred `cmp` operands re-read at the `jcc`; deleted jumps and ordinal tables now reported |
| `0004-linux-veh-and-nv2a` | `src/platform/`, `src/nv2a/` | `AddVectoredExceptionHandler` implemented on `sigaction` (it returned NULL); NV2A MMIO decoder no longer `#if _WIN32`; one shared `CONTEXT` with R8–R15 |
| `0005-placed-virtual-reservations` | `src/kernel/xbox_memory_layout.{c,h}` | `xbox_HeapReserveAt` — a reservation that NAMES its address comes back at that address or not at all (C070) |

Verify that the patches REPRODUCE the vendor tree rather than assuming:

```sh
tools/check_patches.sh      # exits non-zero on missing, extra or differing files
```

It extracts the pinned upstream commit, applies these patches, and diffs the
result against `vendor/xboxrecomp`. Run it before committing a vendor change --
on its first run it found that `0003` was missing six files, so a fresh clone
plus patches produced a different recompiler from the one every result came
from. `git apply --check <patch>` only says a patch still applies; it cannot
see a change that never reached a patch at all.
