# Android release contract

This document is the implementation and evidence contract for the SDL3 Android
APK. The native target and setup shell now exist; S018 remains partial until a
real APK is installed and measured on the supported device tiers.

## Setup and game-file access

The APK has no terminal and opens `XMen2SetupActivity` before starting
`XMen2GameActivity`. It offers native Android Browse actions for the install
folder containing `XMen2.exe` (`ACTION_OPEN_DOCUMENT_TREE`) or a ZIP
(`ACTION_OPEN_DOCUMENT`). `LucentDocumentImport` owns the persisted read grant,
bounded background copy into app-private staging, cancellation, and recovery.
The title validates exactly one `XMen2.exe` before Lucent promotes the staged
selection to the persistent private `game/` leaf; for a ZIP, Lucent validates
and extracts the complete archive into that same transaction before promotion.
The prior valid selection remains usable until the replacement has passed this
complete validation and private promotion. No filesystem path is inferred from
a SAF URI and the APK does not request `MANAGE_EXTERNAL_STORAGE`.

This deliberately makes the first import a one-time copy. It supports cloud
and removable-storage providers correctly, avoids broad device access, and
means later launches never depend on a provider or a working directory. The
setup persists the private source path. A missing or unusable selection returns
to setup with an actionable error.
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

The script cross-builds the pinned FFmpeg subset, configures CMake for
`arm64-v8a`, builds `libmain.so`, and assembles the release APK. Set
`ANDROID_HOME`, `ANDROID_NDK_VERSION`, and a JDK from 17 through 26 first. The
project pins Gradle 9.4.1, the first maintained patch line that officially runs
on Java 26, together with its compatible Android Gradle Plugin 9.2.1 and the
Gradle distribution checksum. Select an installed compatible JDK with
`JAVA_HOME`; the build does not require an older JDK when the pinned toolchain
supports the current one. The generated native contract is
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

## Touch controls

The title owns the safe-area-aware layout and action vocabulary in
`src/input/touch_controls.cpp`, while `src/input/touch_runtime.cpp` converts
SDL contacts to the existing virtual DirectInput pad through
`lucent::touch::Router`. Lucent owns capture, multi-touch, and cancellation,
not the title's action vocabulary. A contact stays with its zone after leaving
the zone until it ends or is canceled. The runtime derives safe-area insets
from SDL and publishes releases on cancellation, rotation, or lifecycle loss.

The landscape layout uses these zones and the existing Xbox-derived action
rows. Internal retail storage names are not player-facing labels: the touch
document uses the action meanings proven by `binding_rows.c` and
`xbox_defaults.c`.

| Zone | Action mapping |
|---|---|
| Left virtual stick | `Forward`, `Backward`, `MoveLeft`, `MoveRight` |
| Bottom-right action cluster | Light attack, heavy attack, use, jump, mutant powers, energy pack, health pack |
| Retail party portraits, top-right | Pointer press/release through the existing Win32 mouse-message path; the retail click handler selects the tapped hero |
| Physical D-pad | Next hero, previous hero, decrease aggression, increase aggression; these retail bindings remain valid |
| Menu buttons | `Pause`, `Stats` |
| Open playfield swipe | Relative camera movement from the contact's Lucent capture origin; no second visible stick |
| Retail health/energy HUD, top-left | The retained CHud draw path, relocated only while touch mode is active |

The layout must leave an inset for cutouts/navigation bars, support at least
the left stick plus two face/shoulder contacts simultaneously, expose a
reconfigure/hide-controls setting, and make touch feedback visible without
changing the input action delivered to the guest. The mapping is derived from
[`xbox_defaults.c`](../src/native/xbox_defaults.c), not invented per screen.
The shipped feedback document mirrors only authored touch controls with text
labels, rather than showing controller glyphs whose internal action names are
misleading. Gesture and portrait hit regions remain invisible, captured zones
highlight, and the persistent Input setting can hide the controls. Held
contacts persist until finger-up/cancel rather than expiring on a test-channel
timeout.

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
