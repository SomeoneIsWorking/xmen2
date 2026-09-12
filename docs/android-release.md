# Android release contract

This document is the implementation and evidence contract for the SDL3 Android
APK. The debug APK and setup shell have been installed on the shared emulator;
S018 remains partial until a signed build is measured on the supported device
tiers.

## Setup and game-file access

The APK has no terminal and opens `XMen2SetupActivity` before starting
`XMen2GameActivity`. It offers a native Android Browse action for a ZIP of the PC install
(`ACTION_OPEN_DOCUMENT`). ZIP input keeps thousands of tiny files in one
sequential transfer, and the shared `android-port` framework owns the persisted read grant,
bounded background copy into persistent package storage, cancellation, and
resumable recovery.
For a selected direct archive, the native title bridge maps the staged file instead of duplicating
it in native heap; X-Men's title policy bounds the known 2.37 GiB PC install
to 4 GiB compressed/expanded, 256 MiB per entry, and 20,000 entries.
The title validates exactly one `XMen2.exe`, every original DLL the native
runner maps beside it, and title-owned content sentinels spanning every
boot-time asset family before the Android framework promotes the staged selection to the
persistent private `game/` leaf; for a ZIP, Lucent's shared safe ZIP helper validates and extracts
the complete archive into that same transaction before promotion. The prior valid
selection remains usable until the replacement has passed this complete
validation and private promotion. After a ZIP extraction passes title validation,
the Android framework discards the staged ZIP before promotion so the retained private install
contains only the extracted game tree; rejected imports discard their bounded
staging instead. No filesystem path is inferred from a SAF URI
and the APK does not request `MANAGE_EXTERNAL_STORAGE`.

The setup supplies notification wording and destination to `AndroidImportProgress`;
the Android framework owns the persistent determinate progress bar when the ZIP size is known,
foreground service, updates and teardown.
The pending external picker identity is saved with Activity state and restored
with a callback belonging to the new Activity before Android delivers its result.
Displaying import progress again on Activity resume does not start another
service; completion, rejection, cancellation, and finishing the Activity stop the
existing service through `Context.stopService`, so a late stop cannot create a
background service during the import-to-game handoff. Foreground promotion
failures propagate instead of being hidden by a plain notification. The Android framework's
`android_import_lifetime` test exercises idle teardown, repeated progress,
import-to-game teardown, late callbacks, cancellation before service creation,
and a refused start. This is lifecycle evidence, not confirmation that the
reported ARM64 boot crash has the same cause.

The imported ZIP and validated `game/` tree live under Android's package OBB
storage, so reinstalling the APK can discover and reuse them without copying
again. An interrupted ZIP copy retains its staging marker and resumes from the
last complete byte when the same document is selected again. It supports cloud
and removable-storage providers correctly, avoids broad device access, and
means later launches never depend on a provider or a working directory. The
setup persists a canonical private source path, so Android's equivalent
`/data/data` and `/data/user/0` aliases cannot make a valid retained install
appear to be outside the app. A missing or unusable selection returns to setup
with an actionable error.
`lucent_platform_set_user_data_directory` receives the Activity's absolute
private files root; there is no Android environment-variable fallback. Saves,
settings, input recordings, and the live-session record all live below that
root -- never in the working directory, which a package does not own and which
is read-only on Android.

The APK also declares `INTERNET`. The product target always opens the agent
control channel on loopback, and creating any socket requires that permission's
`inet` group; without it `socket()` fails with `EACCES` and `control_start()`
exits before the game runs.

Build it with:

```sh
uv run --frozen python tools/build_android.py
```

The script invokes the pinned shared `android-port` native-prefix build (SDL,
SDL_image, FreeType, fmt, FFmpeg, and NDK C++ runtime), configures CMake for
`arm64-v8a` at Android API 21, builds `libmain.so`, and assembles the release
APK. API 21 is the common floor for 64-bit Android ABIs and covers the Android framework's
SAF import path; newer Android calls stay behind runtime guards in the Android framework.
Native prefixes are isolated by Android API and ABI under
`build/deps/android/android-<api>/<abi>/`; a compatibility build cannot
replace the release build's libraries with a lower API floor. Set
`ANDROID_HOME`, `ANDROID_NDK_VERSION`, and a JDK from 17 through 26 first. The
shared prefix contract records that FFmpeg archives are position-independent;
changing that contract invalidates an older prefix instead of allowing a stale
non-PIC static archive to reach the `libmain.so` linker. The project pins
Gradle 9.4.1, the first maintained patch line that officially runs
on Java 26, together with its compatible Android Gradle Plugin 9.2.1 and the
Gradle distribution checksum. Select an installed compatible JDK with
`JAVA_HOME`; the build does not require an older JDK when the pinned toolchain
supports the current one. The shared Android owner selects the paired JDK,
checks that `libmain.so` exports its callable SDL entry, and inspects the
assembled APK for the complete SDL/NDK runtime before publication. The generated native contract is
`build/android-arm64-v8a/x2-android.properties`.
Release assembly also requires the long-lived update key through
`X2_ANDROID_KEYSTORE`, `X2_ANDROID_KEY_ALIAS`, `X2_ANDROID_STORE_PASSWORD`, and
`X2_ANDROID_KEY_PASSWORD`. These are maintainer build inputs, never player
setup inputs or tracked files. The build refuses an unsigned release, verifies
the signature with the SDK's `apksigner`, and stages
`build/release/X-Men-Legends-II-arm64-v8a.apk`.

Local pipeline verification may use an explicitly ephemeral key only if the
artifact stays in Gradle's build output and is never staged or published. On
2026-08-30, Java 26, Gradle 9.4.1, and AGP 9.2.1 completed all 50 release tasks;
`apksigner` verified the resulting v3 signature under the one-day local test
certificate. This proves assembly, not release identity or device fitness.

## Direct gameplay boot for profiling

The desktop and Android maintainer paths share the title's complete
`X2_BOOT_MAP` transition: it calls the retail `startFirstMission` party
initializer and then loads the requested level, so it does not fast-forward a
running world or fabricate a party. For a debug APK, pass the map as an intent
extra. The documented combat map is `act1/deadzone/deadzone1`:

```sh
adb -s <serial> shell am start -n \
  com.someoneisworking.xmen2/.XMen2SetupActivity \
  --es com.someoneisworking.xmen2.debug.boot_map act1/deadzone/deadzone1
```

This extra is accepted only by debug builds and is bounded to a relative map
name. Release builds ignore it. Use the loopback control endpoint after the
level appears to reset timing and collect frame-time percentiles during actual
combat; a boot screenshot or menu run is not gameplay performance evidence.

## Touch controls

Touch play is NOT an Android feature and is not owned here — see
[Touch play](touch-play.md) for the device classification, the zone/action
vocabulary, the safe-area layout, and its tests. It is built and shipped on
every platform; the APK merely consumes it.

What is Android's own is the acquisition edge: the Activity delivers SDL finger
events like any other host, and the packaged build must not regress the safe
area on devices with cutouts or gesture navigation bars.

## Performance gate

The M2 Air observation is useful for desktop investigation but is not Android
evidence. Before calling the APK performant, record at least one run on each
supported device tier (low, target, and high) with the same boot/map and asset
set. Each run records renderer/backend, resolution, frame-time p50/p95/p99,
startup and level-load time, resident memory, audio state, and thermal/throttle
behavior for a sustained 20-minute play session. A failed tier remains failed;
the release must not silently lower fidelity or frame pacing to hide it.

The evidence must include a cold setup launch, a document-picker return, the
first movie, a representative combat scene, touch-only input, and suspend/resume.
No desktop result substitutes for this gate, and no frame-rate cap or reduced
render path may be enabled merely to make a device pass.

Use the shipping loopback control endpoint through a single explicit collector:

```sh
uv run --frozen python tools/android_qualify.py --serial <adb-serial> --tier <low|target|high> \
  --startup-ms <cold-start-ms> --level-load-ms <representative-level-load-ms> --audio-verified \
  --scenario cold-setup --scenario picker-return --scenario first-movie \
  --scenario combat --scenario touch-only --scenario suspend-resume
```

The game must already be running on that device. The collector creates and
removes only its exact `adb forward` mapping, resets the frame-time window at
the guest-input boundary, then samples the runner's bounded exact p50/p95/p99
frame timings. It records `dumpsys meminfo` PSS and thermal-service observations
for the full 20-minute interval, and writes one replace-in-place JSON report at
`scratch/run/android-qualification.json`. It refuses a reset that did not reach
the guest, shorter runs, missing scenarios, absent frame samples, a
non-presenting renderer, or an ambiguous/offline device. The report is release
evidence, not a release decision: all three named tiers, the signed APK, and
manual rendering/audio correctness review remain required before publication.
