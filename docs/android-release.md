# Android release contract

This document is the implementation contract for a future SDL3 Android APK.
It is not a claim that the APK exists today; the current state is recorded as
S018 in [`project-state.md`](project-state.md).

## Setup and game-file access

The APK has no terminal and must open an initial setup screen before starting
the game. The screen has a Browse button that launches Android's Storage Access
Framework document picker. It accepts either the user's `XMen2.exe` or a ZIP
containing exactly one `XMen2.exe` anywhere below its root.

The Activity persists the selected document URI with persistable read access.
Because a content URI is not necessarily a filesystem path, the Android shell
copies the selected document into app-private storage before calling the shared
Lucent ZIP extractor. The extracted install and the URI preference are app data,
not checkout, APK, or cache/scratch data. A missing or revoked URI returns to
the setup screen with an actionable error.

## Touch controls

The shell owns the safe-area-aware layout and maps touch contacts through
`lucent::touch::Router`; Lucent owns capture, multi-touch, and cancellation, not
the title's action vocabulary. A contact stays with its zone after leaving the
zone until it ends or is canceled.

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
| Right virtual stick | `CameraUp`, `CameraDown`, `CameraLeft`, `CameraRight`; press is `MapToggle` |

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
