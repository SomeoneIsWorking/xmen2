---
id: 136
title: Android APK shell has no native Android target
status: open
symptom: Android setup/native packaging exists, but no installed-device or measured mobile run exists
state_items: S018
tags: android,apk,sdl3,touch,performance
created: 2026-08-30
updated: 2026-08-30
---

## Root cause

The desktop and Android targets have different dependency and lifecycle
contracts. Host pkg-config cannot describe Android ARM64 libraries, and SDL's
desktop picker cannot acquire Android content URIs. The Android target now has
an explicit cross-compiled FFmpeg prefix, Gradle/Activity/JNI shell, SAF copy
boundary, and touch publication path; the remaining gap is installed-device
and performance evidence.

## What was tried / dead ends

The title-specific touch action/layout owner was added and tested. The setup
and native packaging paths are now implemented, but a desktop build or M2 Air
observation still cannot substitute for Android device evidence.

## Proper fix

Build and install the real SDL3 Android application target using
`tools/build_android.py`. Keep URI acquisition and app-private staging in the
Android shell, reuse Lucent ZIP extraction, route SDL contacts through
`src/input/touch_controls.cpp`, and connect the resulting actions to the guest
input publication boundary. Measure the APK on low, target, and high Android
tiers before changing S018 to verified.
