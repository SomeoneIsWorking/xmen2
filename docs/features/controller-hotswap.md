# Controller hotswap

Feature 1 of the three (`README.md`). **Working**, through the game's own
mechanism rather than around it.

## What the game does, and why it needed nothing invented

`XMen2.exe` enumerates game controllers exactly once. There is no
`WM_DEVICECHANGE` anywhere in it, so a pad connected after startup is invisible
for the rest of the run -- that is the gap this feature closes.

But the enumeration is not a one-shot routine. `FUN_00628e20` is
`__thiscall enumerateControllers(BOOL bRecordNew)` and it:

1. stores its argument at `this+2`;
2. clears the attached flags at `this+0x4e4`;
3. calls `EnumDevices(DI8DEVCLASS_GAMECTRL, FUN_00628e00, this,
   DIEDFL_ATTACHEDONLY)`;
4. clears `this+2` again on the way out.

That flag is the whole reason this feature has the shape it does. The
per-device callback `FUN_00628b40` checks it before recording a controller's
instance GUID in the ten-slot table at `this+0x27e8` -- and at `0x00628c5b` it
searches that same table again and **returns without storing the device** if
the GUID is not in it. So a controller the game has not been told to record is
created, configured, and then never polled.

That is not a hypothesis. The first implementation here called the per-device
callback directly, and produced exactly that: the pad was created, given its
data format and its axis range, and read **zero** times.

## What this host does

When the controller inventory changes, `dinput8_hotplug_pump` calls
**`FUN_00628e20(TRUE)`** -- the game's own routine, with the same argument
startup passes. No host code writes guest state; arrivals and removals are
applied by the game's own rules.

The routine's address is not hardcoded. `x86_native_entry_containing` answers
"which function is my caller in", asked at the moment the game calls
`EnumDevices`, so the host identifies the routine by finding itself inside it.
It reports what it found (`0x00628e20 (FUN_00628e20)`) so a wrong answer is
visible rather than silent.

The pump runs from keyboard `GetDeviceState`, which `FUN_006285c0` makes at
`0x0062861e` before the joystick loop. It is not gated by `Present`, so it sees
changes while rendering is paused. A monotonic inventory generation makes
unchanged polls no-ops and admits each changed generation once. Re-enumerating
inside the joystick loop would insert into a table the game is walking.

Two lifetimes that reuse an inventory slot are not the same controller. Each
guest `IDirectInputDevice8` stores the live instance GUID it was created for
and resolves that GUID on every operation. After disconnect, the old object
continues returning `DIERR_INPUTLOST`; a new controller in the same host slot
gets a distinct object. The device registry grows by connection lifetime, not
by the eight-pad simultaneous limit, because the guest may retain old objects.

The earlier implementation retained eight "already offered" GUIDs for the
whole process. Once full, every later poll treated the current GUID as new and
re-enumerated. Generations replace that category error: the simultaneous-device
limit is never used as a lifetime limit. `test_controller_hotplug`,
`test_controller_instance`, and the virtual pad's twelve-cycle slot-reuse test
exercise these production seams.

**Unplug** is the other half. `Poll`, `GetDeviceState` and `Acquire` all answer
`DIERR_INPUTLOST` once the pad is gone, which is what Windows answers and what
`FUN_006285c0` tests for by value at `0x00628621`. Acquire failing matters as
much as Poll failing: an Acquire that succeeded on a vanished pad would leave
the game alternating between the two calls forever, believing it had a
controller.

## Measured

`X2_VIRTUAL_PAD=f1500` attaches a synthetic pad at frame 1500, well after
enumeration. The run reports:

    DINPUT8: the game's gamepad enumeration routine is 0x00628e20 (FUN_00628e20)
    DINPUT8: HOTSWAP -- pad 0 ("X2 Virtual Pad") appeared after the game had
             enumerated. Calling the game's own enumeration routine ...
    gamepad  272 byte state, acquired, 2387 state read(s), 2388 Poll(s), 1 Acquire(s)

and with `X2_VIRTUAL_PAD=f1200-2200` (attach, then unplug) the state reads stop
at the unplug while the polls continue -- a disconnected controller reporting
as disconnected rather than as a connected one nobody is touching.

## Assignment and prompts

Settings use one device-assignment grid: keyboard profiles and persistent
controllers are rows; Unassigned and Players 1–4 are columns. A row has one
owner. Player 1 may own one keyboard and one controller simultaneously; that is
hotswap, so both binding sources remain published and controller-specific
tutorial prose follows the last active source. Players 2–4 own one device kind
each and join through an edge of that assigned device's Start action.

Persistence requires an identity SDL can tie to the physical device: a serial
number first, otherwise a stable OS path. A backend exposing neither receives a
distinct live-session identity so identical connected units still work. The
grid labels it session-only and permits a process-lifetime assignment without
saving it. That assignment binds the immutable live GUID: after disconnect it
stays assigned-but-unresolved until explicitly cleared, never falling back to a
persisted controller or a different device that reused the slot. The probe
reports the identity quality beside every controller.

Modal RmlUi input capture writes a real neutral DIJOYSTATE2: all axes at the
configured midpoint, all buttons clear and every POV at `0xffffffff`. Zeroing
the structure would hold POV 0 north while the settings overlay was open.

The live input probe reports inventory generation, live GUIDs, identity quality
and resolved player ownership. The visible RmlUi grid rebuilds when
the inventory generation changes.

## Not done

The engine's OWN hooks -- `igControllerManager::_controllerConnectionFunction`
and `_controllerDisconnectionFunction` (`libIGDisplay`) -- are still unwired.
They belong to the DirectInput **7** path, which this game's exe does not use
for its controllers; wiring them matters only if something in the engine (as
opposed to the game) needs to hear about a controller arriving.
