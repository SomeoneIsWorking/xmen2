---
id: 136
title: Android APK shell has no native Android target
status: open
symptom: No Android Activity, document-provider bridge, APK packaging target, or measured mobile run exists
state_items: S018
tags: android,apk,sdl3,touch,performance
created: 2026-08-30
updated: 2026-08-30
---

## Root cause

The native target is currently a POSIX desktop host: it depends on POSIX
process, memory, filesystem, threading, and FFmpeg/pkg-config facilities, and
its first-run picker is SDL3 desktop UI. There is no Android Gradle/Activity/JNI
target that can persist a document URI, copy the selected file into app-private
storage, invoke Lucent ZIP extraction, or publish touch actions into the guest
input path.

## What was tried / dead ends

The title-specific touch action/layout owner was added and tested, but a
manifest-only or setup-only APK would not launch the game and would falsely look
like Android support. Desktop and M2 Air performance cannot substitute for
Android device evidence.

## Proper fix

Add a real SDL3 Android application target and Activity/native bridge. Keep URI
acquisition and app-private staging in the Android shell, reuse Lucent ZIP
extraction, route SDL contacts through `src/input/touch_controls.cpp`, and
connect the resulting actions to the guest input publication boundary. Build
and measure the resulting APK on low, target, and high Android tiers before
changing S018 to verified.
