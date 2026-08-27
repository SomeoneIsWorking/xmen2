---
id: 134
title: RelWithDebInfo disables assert-backed test checks
status: resolved
symptom: Normal CTest reports PASS while assert expressions are compiled out under NDEBUG
tags: tests,cmake,clang,verification
created: 2026-08-27
updated: 2026-08-27
---

# RelWithDebInfo disables assert-backed test checks

- status: resolved
- tags: tests, cmake, clang, verification

## Symptom

The normal `scratch/build-native` RelWithDebInfo build reports PASS for C test executables whose checks and side effects are inside `assert(...)`, because CMake defines `NDEBUG`.

## Root cause

Test executables inherit the production configuration macro without a test-target policy that undefines `NDEBUG`. At least 23 existing test sources still use `assert`, so a green CTest result does not prove those expressions ran.

## Resolution

`CMakeLists.txt` now undefines `NDEBUG` on every project `test_*`
executable while leaving production targets unchanged.  The dedicated
`test_assertions_enabled` target refuses to compile when `NDEBUG` survives and
also verifies that an assertion expression executes.

The real `scratch/build-native` tree is configured as `RelWithDebInfo` with
Clang.  Ninja's emitted compile command contains `-DNDEBUG -UNDEBUG`, the new
target compiled, and CTest ran its assertion expression successfully.  The
combined suite remains the landing gate for the surrounding milestone.
