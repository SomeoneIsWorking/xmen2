# Gameplay input: from a bound action to the controller record

## The per-frame record

`CPlayerController` (`FUN_00554840`) clears its record every frame — a held
mask at `+0x2f8` and a float per slot from `+0x2fc` — and hands both to
`FUN_0061a810`, which fills them with one call to `FUN_0061a4c0` per
(slot, action):

```
cdecl float resolve(uint32_t *held, float *slots, int slot, int action,
                    const int *player, float sign)
```

`FUN_0061a4c0` asks the controller manager (`FUN_00551ed0`, vtable `+0x3c`)
for the player's binding row, reads the action's value with `FUN_00629630`
(the largest magnitude over the action's four bindings), and when
`|value| > 0.75` (the float at `0x00682c10`) sets `1 << slot` in `*held` and
stores `|value|`, times `sign` when `sign` is non-zero, into `slots[slot]`.
It returns `slots[slot]` in `ST(0)` either way.

A stick binding resolves to its positive half, `0..1`: `FUN_00627650` scales
the DirectInput axis (range `[-1000, 1000]`) by `0.001` and `FUN_006273b0`
keeps the half the binding code names. So the value is analog up to here.

Only eight calls pass a sign, and they are the axes:

| slot | actions (sign) | meaning |
|---|---|---|
| 1 | Forward (+1), Backward (−1) | movement Y |
| 0 | MoveLeft (−1), MoveRight (+1) | movement X |
| 3 | CameraUp (+1), CameraDown (−1) | camera Y |
| 2 | CameraLeft (−1), CameraRight (+1) | camera X |

Every other call (attacks, powers, triggers, potions, the d-pad actions)
passes sign 0, and for those the 0.75 is a press point.

## Why a stick steered like a key pad

The 0.75 gate is per axis. A full diagonal is 0.71 on each axis and was
dropped whole; a push at 30° kept one axis and lost the other; any push under
three quarters did nothing. Measured headless through the touch stick before
the change: 18% deflection did not move the hero, full deflection ran. The
game could be steered in eight directions only — on a real pad too.

## What the character does with the pair

The movement code treats (slot 0, slot 1) as a vector. Measured headless with
the gate removed, through the touch stick (`X2_VIRTUAL_PAD=1`,
`--no-window --d3d8 --control`, `/touch` drags):

- It starts walking at a length of about 0.3, in any direction: 0.283 along
  one axis stays still, 0.326 walks; a diagonal of length 0.40 (0.284 per
  axis) walks. That minimum is radial and it is the game's own.
- Speed grows with length. In 0.6 s the camera, which follows the hero, moved
  about 60 px at 0.40, about 106 px at 0.67 and 120–160 px at 1.0.

So the Xbox-style analog walk/run was there all along; the PC resolver's
per-axis gate was the only thing in the way.

## The port's answer

`src/native/stick_axis_override.c` overrides `FUN_0061a4c0` for the eight
signed calls only: any non-zero value is stored, with the retail side effects
(held bit, signed store, `ST(0)` return, `EAX` = `slots`). The sign-0 calls run
the retail body unchanged.

The dead zone the 0.75 used to supply moves to where both components of a
stick are known: the pad sample (`dinput_pad_sample.c`) applies a radial dead
zone with XInput's recommended radii (`pad_stick_dead_zone.h`) and rescales
past it. The synthetic pad that the touch stick drives is exempt, because the
touch stick has its own dead zone (`src/input/thumb_stick.cpp`).

The touch stick also starts its travel at the game's walk minimum
(`ThumbStick::kWalkStart`, 0.33): past its own small dead zone the first bit
of travel is the slowest walk, so the whole ring steers instead of its inner
third doing nothing.
