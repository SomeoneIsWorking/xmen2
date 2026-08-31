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
desktop picker cannot acquire Android content URIs. The Android target now has
an explicit cross-compiled FFmpeg prefix, Gradle/Activity/JNI shell, SAF copy
boundary, touch publication/feedback path, correct native Activity entry point,
and fail-closed release signing contract. Selection now proves the complete
original PC image set required by the native loader (generated from
`X2_MODULES`) before Lucent promotes a direct install or nested ZIP. Gradle 9.4.1 and Android Gradle
Plugin 9.2.1 now make the installed Java 26 JDK a supported build path.
The build rejects split `java`/`javac` homes. A 50-task release assembly signed
with a one-day local verification key passed `apksigner` v3 verification; it was
not staged or treated as publishable. The remaining gap is a long-lived
maintainer signing key and installed-device/performance evidence. The shipping
control endpoint now supplies exact bounded p50/p95/p99 frame times, and
`tools/android_qualify.py` records a fail-closed 20-minute named-device
collection with PSS, thermal-service observations, and each required manually
exercised scenario.

An API 35 x86-64 emulator exercised a real DocumentsUI folder return against a
loader-image-only source: the title validator accepted all twenty-one required
PE images, Lucent promoted them atomically to app-private `game/`, and a cold
relaunch entered SDL and mapped every loader image. The first implementation
then falsely returned to setup because it compared Android's `/data/data` and
`/data/user/0` aliases as raw paths. Canonical, path-boundary-aware containment
now admits the retained private selection. The reduced source intentionally
lacks gameplay archives, so its clean post-loader exit is not gameplay evidence.

## What was tried / dead ends

The title-specific touch action/layout owner, shared-SVG feedback document,
persistent hide setting, lifecycle cancellation, and held-contact semantics
were added and tested. NDK 28 Clang linked the real ARM64 `libmain.so`; its
exported `main` matches `XMen2GameActivity.getMainFunction()`. The release tool
now refuses unsigned output and verifies signed output with `apksigner`. A
desktop build or M2 Air observation still cannot substitute for Android device
evidence, and the pre-existing unsigned APK is not a release candidate.

## Proper fix

Supply the long-lived Android release keystore, then build and install the real
SDL3 target using `tools/build_android.py`.
Exercise setup selection for both a direct install and nested ZIP, touch-only
gameplay, suspend/resume, audio, and the launcher/update signature. Measure the
APK on low, target, and high Android tiers before changing S018 to verified.
