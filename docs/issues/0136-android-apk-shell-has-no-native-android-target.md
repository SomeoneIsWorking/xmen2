---
id: 136
title: Android APK lacks signed installed-device and performance evidence
status: open
symptom: Android setup/native packaging exists, but no signed installed-device or measured mobile run exists
state_items: S018
tags: android,apk,sdl3,touch,performance
created: 2026-08-30
updated: 2026-08-31
---

## Root cause

The desktop and Android targets have different dependency and lifecycle
contracts. Host pkg-config cannot describe Android ARM64 libraries, and SDL's
desktop picker cannot acquire Android content URIs. The Android target now
consumes the shared cross-compiled Android prefix, plus a Gradle/Activity/JNI
shell, SAF copy boundary, touch publication/feedback path, correct native Activity entry point,
and fail-closed release signing contract. Selection now proves the original PC
image set required by the native loader (generated from `X2_MODULES`) plus
title-owned content sentinels across the boot-time asset families before Lucent
promotes a direct install or nested ZIP. Gradle 9.4.1 and Android Gradle
Plugin 9.2.1 now make the installed Java 26 JDK a supported build path.
The build rejects split `java`/`javac` homes. A 50-task release assembly signed
with a one-day local verification key passed `apksigner` v3 verification; it was
not staged or treated as publishable. The remaining gap is a long-lived
maintainer signing key and installed-device/performance evidence. The shipping
control endpoint now supplies exact bounded p50/p95/p99 frame times, and
`tools/android_qualify.py` records a fail-closed 20-minute named-device
collection with PSS, thermal-service observations, and each required manually
exercised scenario.

An API 35 x86-64 emulator exercised a real DocumentsUI folder return and
revealed that the former loader-image-only validator could promote an
unplayable selection. The validator now rejects that reduced selection before
promotion by requiring title-owned content sentinels as well as every PE image.
After installing the rebuilt debug APK, its retained incomplete selection was
refused and `XMen2SetupActivity` remained the resumed Activity. Canonical,
path-boundary-aware containment still admits valid retained selections through
Android's `/data/data` and `/data/user/0` aliases. The emulator was then
recreated with a 16 GiB data image and completed a full 1.57 GiB ZIP import to
a 2.37 GiB app-private installation. The native runner reaches the executable
but faults when `igArenaMemoryPool` calls
`igPthreadSemaphore::obtainResource`: the pool's semaphore field is
`0xffffffec`, an invalid guest address. The equivalent desktop path succeeds.
This moves the blocker from setup/storage capacity to an Android runtime or
allocator divergence; it prevents gameplay, touch visual verification, and
performance evidence.

## What was tried / dead ends

The title-specific touch action/layout owner, shared-SVG feedback document,
persistent hide setting, lifecycle cancellation, and held-contact semantics
were added and tested. NDK 28 Clang linked the real ARM64 `libmain.so`; its
exported `main` matches `XMen2GameActivity.getMainFunction()`. The release tool
now refuses unsigned output and verifies signed output with `apksigner`. A
desktop build or M2 Air observation still cannot substitute for Android device
evidence, and the pre-existing unsigned APK is not a release candidate.

A separate x86-64 trace APK compiles against the same Android prefix without
changing the shipping Ninja tree. Its debug-only setup Intent accepts only the
existing bounded `X2_ARGS` entry-point grammar and cap, never arbitrary process
environment settings. Before that trace could be installed, the retained 16 GiB
API-35 AVD began dropping its ADB transport during Android boot. Its QCOW2 image
and host capacity are present, but this device-runtime blocker is distinct from
the native semaphore fault; no trace result was inferred from it.
Recreating exactly that shared AVD with a fresh 32 GiB data partition did not
change the failure: the emulator launcher itself segfaults before Android boot
with both SwiftShader and software GPU modes. The storage image is therefore
not its cause. Fedora's audit log identifies the host cause: SELinux denies
`qemu-system` executable-heap access, after which its `RenderThread` receives
SIGSEGV. `audit2allow` cannot provide a narrow local module: the emulator is
labelled `unconfined_t`, so its proposed `execheap` rule would apply to every
process in that domain. A port change cannot correct this host policy defect.

## Proper fix

Supply the long-lived Android release keystore, then build and install the real
SDL3 target using `tools/build_android.py`.
Exercise setup selection for both a direct install and nested ZIP, touch-only
gameplay, suspend/resume, audio, and the launcher/update signature. Measure the
APK on low, target, and high Android tiers before changing S018 to verified.
Resolve the Android semaphore/allocator divergence first, then repeat the
complete ZIP and direct-install flows through to gameplay before qualifying
touch controls and performance.
Before collecting the new trace, repair the Fedora Android Emulator SELinux
integration so QEMU receives a dedicated confined label with only the required
access. Do not install the broad `unconfined_t` `audit2allow` rule or disable
SELinux system-wide.
