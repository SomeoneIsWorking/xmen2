# Android release contract

This document is the implementation and evidence contract for the SDL3 Android
APK. The native target and setup shell now exist; S018 remains partial until a
real APK is installed and measured on the supported device tiers.

## Setup and game-file access

The APK has no terminal and opens `XMen2SetupActivity` before starting
`XMen2GameActivity`. The setup screen has a Browse button that launches
Android's Storage Access Framework document picker. A ZIP is copied into
private storage and accepted when it contains exactly one `XMen2.exe` at any
nested path. If the user first chooses `XMen2.exe`, Android then requests the
containing install folder: a single content URI grants access to one file, not
its siblings, so pretending that the executable alone is a complete install
would produce a broken launch. The folder is copied recursively and native
validation still requires exactly one executable.

The Activity persists the selected document URI when the provider allows
persistable read access. Because a content URI is not necessarily a filesystem
path, the Android shell copies the selected document/tree into app-private
storage before calling the shared Lucent ZIP extractor or the native folder
validator. The staged install and source preference are app data, not checkout,
APK, or cache/scratch data. A missing selection returns to setup with an
actionable error. `lucent_platform_set_user_data_directory` receives the
Activity's absolute private files root; there is no Android environment-variable
fallback.

Build it with:

```sh
uv run --frozen python tools/build_android.py
```

The script cross-builds the pinned FFmpeg subset, configures CMake for
`arm64-v8a`, builds `libmain.so`, and assembles the release APK. Set
`ANDROID_HOME`, `ANDROID_NDK_VERSION`, and a JDK from 17 through 26 first. On
DNF-based systems, install the required JDK with
`sudo dnf install java-17-openjdk-devel`, then set `JAVA_HOME` to it. The
generated native contract is `scratch/build-android-arm64/x2-android.properties`.

## Touch controls

The title owns the safe-area-aware layout and action vocabulary in
`src/input/touch_controls.cpp`, while `src/input/touch_runtime.cpp` converts
SDL contacts to the existing virtual DirectInput pad through
`lucent::touch::Router`. Lucent owns capture, multi-touch, and cancellation,
not the title's action vocabulary. A contact stays with its zone after leaving
the zone until it ends or is canceled. The runtime derives safe-area insets
from SDL and publishes releases on cancellation, rotation, or lifecycle loss.

The initial landscape layout uses these zones and the existing Xbox-derived
action rows:

| Zone | Action mapping |
|---|---|
| Left virtual stick | `Forward`, `Backward`, `MoveLeft`, `MoveRight` |
| Face cluster A/B/X/Y | `LowAttack`, `HighAttack`, `Guard`, `Jump` |
| Right shoulder buttons | `Power`, `Ally` |
| Right bumper | `TargetLock` |
| D-pad cluster | `NextHero`, `PreviousHero`, `DecreaseAggr`, `IncreaseAggr` |
| Menu buttons | `Pause`, `Stats` |
| Right virtual stick | `CameraUp`, `CameraDown`, `CameraLeft`, `CameraRight`; its center click is `MapToggle` |

The layout must leave an inset for cutouts/navigation bars, support at least
the left stick plus two face/shoulder contacts simultaneously, expose a
reconfigure/hide-controls setting, and make touch feedback visible without
changing the input action delivered to the guest. The mapping is derived from
[`xbox_defaults.c`](../src/native/xbox_defaults.c), not invented per screen.

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
