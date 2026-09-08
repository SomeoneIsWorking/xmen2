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
