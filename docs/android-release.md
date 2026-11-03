# Android release contract

This document is the implementation and evidence contract for the SDL3 Android
APK. The debug APK and setup shell have been installed on the shared emulator;
S018 remains partial until a signed build is measured on the supported device
tiers.

## Launcher icon

The CMake Android branch renders `packaging/xmen2-port.svg` into
`<build>/android-res/mipmap-*/xmen2_port_icon.png`
(`tools/android_launcher_icon.py`); Gradle adds that directory to the APK's
resources through `x2.resDir`.

## Setup and game-file access

The APK has no terminal and opens `XMen2SetupActivity` before starting
`XMen2GameActivity`. It offers a native Android Browse action for a ZIP of the PC install
(`ACTION_OPEN_DOCUMENT`). ZIP input keeps thousands of tiny files in one
sequential transfer, and the shared `android-port` framework owns the persisted read grant,
bounded background copy into package storage, cancellation, and
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

The install lives in a folder the player picks in shared storage
(`ACTION_OPEN_DOCUMENT_TREE`, suggested `Documents/X-Men Legends 2`), which an
uninstall does not remove. Its layout is title-owned: `game/` holds the
validated directory that contains `XMen2.exe`, and `saves/` holds the profile
root (`x2native.conf`, `registry.txt` and the retail `Activision/X-Men Legends
2/Save` leaf). Package OBB storage holds only the resumable ZIP staging and its
private extraction; after title validation the shared framework's
`AndroidDocumentTree.publish` copies the extracted directory into the folder as
`game/` and the staging is discarded. A release that kept `game/` in OBB is not
migrated: the player imports once into a folder, and the old OBB tree is deleted
when the folder install is first accepted.

The folder grant dies with the package, so after a reinstall the setup screen
offers **Use existing install folder**. Picking a folder whose `game/XMen2.exe`
passes the title's identity check (`install_validate_executable`: PE header,
every mapped DLL, the content sentinels) starts the game with no copy; only a
missing or invalid tree asks for the ZIP. A remembered folder whose grant still
persists is validated in place on every launch.

Native code never sees a filesystem path for the folder: files a previous
install created are not readable by path under scoped storage. The bridge mounts
the folder at the synthetic root `/document-tree` in `src/native/host_files`,
the one layer every resolved host path goes through (guest `CreateFile`, CRT
`fopen`, find/attributes/delete/directory calls, PE image maps, SFD movies,
install validation, settings, registry and autosave storage). A path under the
mount is answered by `android_document_tree.cpp` over JNI from the framework's
cached document index, and opened as a descriptor from
`ContentResolver.openFileDescriptor`; every other path is the host filesystem.
Directory listings from either backend come back in NTFS order (`.` and `..`
first, then names compared upper-cased): a provider lists children in its own
order, and Alchemy's `igPluginHelper::loadAllPlugins` registers the libraries in
listing order, so a provider's order failed the front-end `movie` menu with an
allocation of -2 bytes. Publishing a 13,383-file install into the folder takes
about 17 minutes on the API 35 emulator, one provider copy per file.
`host_replace` is the one way a finished file takes its final name. A provider
cannot rename over a file, so `AndroidDocumentTree.replace` renames the written
file to `<name>.new`, deletes `<name>`, then renames `<name>.new` to `<name>`;
`<name>.new` therefore only ever names a complete file. The next open, stat or
listing that meets a `<name>.new` finishes the swap, so a kill at any step leaves
the old or the new settings or autosave, never neither. Writers use another
temporary name (`settings.cpp` writes `<name>.part`). The touch HUD's power icons
are read through `host_fopen` and parsed with Alchemy's `igb_open_bytes`.

An interrupted ZIP copy
retains its staging marker and resumes from the last complete byte when the
same document is selected again. A missing or unusable selection returns to
setup with an actionable error.
`lucent_platform_set_user_data_directory` still receives the Activity's absolute
private files root, because what lives there cannot go through a document
provider: the RmlUi resources, run logs (a crash report is written from a
signal handler), the runtime CVar file, input recordings and the live-session
record. The bridge sets the profile root that `shell32.cpp:save_dir()` owns to
the folder's `saves/` through the existing save-directory override, so saves
and settings follow the folder and every save owner keeps deriving its leaf from
that one root. Nothing lives in the working directory, which a package does not
own and which is read-only on Android.

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

Launch on a device with `am start`, never `adb shell monkey`: monkey's default
rotation events unlock rotation and leave the phone's auto-rotate on (#193).

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
