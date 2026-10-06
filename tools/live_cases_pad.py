"""Live cases for pads, the touch pad and the stick."""

from __future__ import annotations

import signal
import time
from pathlib import Path

from live_harness import Case, TUTORIAL_MAP
from live_game import (
    live_stick,
    live_viewport,
    pad_poll_rate,
    png_mean_diff,
    reach_authored_conversation,
    wait_controls_unlocked,
    wait_pad_polled,
)


def case_pad_late(case: Case) -> None:
    """A controller attached AFTER game start is admitted by the game's own
    enumeration, assignable as Player 1, and its Start acts in gameplay."""
    case.prepare_profile(["boot.mode=normal"])
    case.launch({
        "X2_BOOT_MAP": TUTORIAL_MAP,
        "X2_VIRTUAL_PAD": "f600",
    })
    case.wait_control(60)

    reached = reach_authored_conversation(case, 240)
    case.check("authored conversation reached (gameplay context)", reached)
    if not reached:
        return
    code, body = case.http("/key?name=Escape&hold=0.4")
    case.check("keyboard Escape cleared the authored scene", code == 200,
               body.decode(errors="replace").strip())
    case.check("controls unlocked after the skip",
               wait_controls_unlocked(case, 180))
    case.check("synthetic pad attached late",
               case.wait_log("DINPUT-PAD: pad 0 connected", 60))
    case.check("hotswap re-entered the game's enumeration",
               case.wait_log("DINPUT8: HOTSWAP", 60))
    code, body = case.http("/assignment?player=1&pad=0")
    case.check("pad assigned to player 1 over the control channel",
               code == 200, body.decode(errors="replace").strip())
    time.sleep(1.0)

    before = case.shot("before-start")
    code, body = case.http("/pad?button=start&hold=2.0")
    case.check("Start press delivered to the synthetic pad", code == 200,
               body.decode(errors="replace").strip())

    responded, worst = False, 0.0
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and not responded:
        current = case.shot("after-start")
        worst = png_mean_diff(before, current)
        responded = worst > 8.0
        time.sleep(1.0)
    case.check("the presented frame changed after Start "
               "(mean |delta| %.1f > 8)" % worst, responded)


def case_touch_pad(case: Case) -> None:
    """The overlay's pad exists BEFORE the guest enumerates controllers, so
    the game offers it to its own callback and then polls it.

    This is the whole reason touch does anything. A pad attached on the first
    finger is attached after the one enumeration this port can observe, and
    the game never polls a controller it was not offered -- measured in a
    browser as 48 contacts published to a pad read 0 times. touch_controls is
    forced ALWAYS so the case does not need a touchscreen; the pad's timing,
    not the finger, is what is under test.
    """
    case.prepare_profile(["boot.mode=normal", "input.touch_controls=2"])
    case.launch({"X2_BOOT_MAP": TUTORIAL_MAP})
    case.wait_control(60)

    case.check("the overlay attached its own pad, with no X2_VIRTUAL_PAD",
               case.wait_log("DINPUT-PAD: pad 0 connected", 120))
    offered = case.wait_log("EnumDevices(class=4 GAMECTRL, flags=0x1) offered",
                            120)
    case.check("the game enumerated controllers and was offered that pad",
               offered,
               "" if offered else "the enumeration found NO device, so the "
               "pad was attached too late for the guest to see it")

    reached = reach_authored_conversation(case, 240)
    case.check("authored conversation reached (gameplay context)", reached)
    if not reached:
        return
    case.http("/key?name=Escape&hold=0.4")
    case.check("controls unlocked after the skip",
               wait_controls_unlocked(case, 180))
    case.check("pad assigned to player 1",
               case.http("/assignment?player=1&pad=0")[0] == 200)
    time.sleep(1.0)

    before = case.shot("before-start")
    case.check("Start press delivered to the pad",
               case.http("/pad?button=start&hold=2.0")[0] == 200)
    responded, worst = False, 0.0
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and not responded:
        worst = png_mean_diff(before, case.shot("after-start"))
        responded = worst > 8.0
        time.sleep(1.0)
    case.check("the presented frame changed after Start "
               "(mean |delta| %.1f > 8)" % worst, responded)


def case_pad_after_load(case: Case) -> None:
    """Issue #117: a controller attached AFTER a save load. pad-late proves
    the same pad works when the run never loaded a payload, so this case
    isolates the load itself. It ends on the POLL side -- FUN_006285c0's own
    ten device-interface pointers and its per-frame polled mask -- because
    "the game reads nothing" has several causes and only that array
    distinguishes them."""
    case.prepare_profile(["boot.mode=continue"])
    case.seed_save("autosave.save")
    case.launch({
        "X2_FILES": "1",
        "X2_SCRIPTS": "1",
        "X2_VIRTUAL_PAD": "f2000",
    })
    case.wait_control(60)

    case.check("the save was loaded through boot Continue",
               case.wait_log("BOOT MODE:", 180))
    case.check("the destination map opened", case.wait_log(".pkgb", 300))
    case.check("controls unlocked after the replayed conversations",
               wait_controls_unlocked(case, 300))

    case.check("the synthetic pad attached after the load",
               case.wait_log("DINPUT-PAD: pad 0 connected", 180))
    case.check("hotswap re-entered the game's enumeration",
               case.wait_log("DINPUT8: HOTSWAP", 120))
    code, body = case.http("/assignment?player=1&pad=0")
    case.check("pad assigned to player 1 over the control channel",
               code == 200, body.decode(errors="replace").strip())
    time.sleep(2.0)

    # The poll side, printed in full whatever it says. A slot the game never
    # reads is a slot whose interface pointer is NULL (or whose GetDeviceState
    # fails) -- nothing else in FUN_006285c0 can gate it -- so this report
    # either names the gate or rules that shape out.
    report = case.get_text("/input?controller=0")
    (case.dir / "poll-side.txt").write_text(report)
    # Only the poll-side block: the report has other "slot N ..." lines (the
    # live-GUID table), and counting those would make ten slots read as more.
    poll, inside = [], False
    for line in report.splitlines():
        if "dinput8 poll side" in line:
            inside = True
        if inside:
            poll.append(line.rstrip())
        if "slot(s) hold a device interface" in line:
            inside = False
    print("\n".join(poll) if poll else
          "  (the poll-side probe printed NOTHING -- that is a probe defect)")
    case.check("the poll-side probe reported all ten slots",
               sum(1 for line in poll if line.strip().startswith("slot ")) == 10)
    holds = any("0 of 10 slot(s) hold a device interface" not in line
                and "slot(s) hold a device interface" in line
                for line in poll)
    case.check("the manager holds at least one device interface after the "
               "post-load admission", holds,
               next((line.strip() for line in poll
                     if "hold a device interface" in line), "no summary line"))

    # Issue #117's own falsifier is a heartbeat that GROWS by thousands, not
    # a single polled frame: a probe-driven read would satisfy the mask check
    # while the game's own loop stayed dead.
    baseline = pad_poll_rate(case)
    polled = wait_pad_polled(case, 60)
    case.check("the game's own loop keeps reading the pad (heartbeat grew "
               "past the probe baseline)", polled,
               "%d -> %d button read(s)" % (baseline, pad_poll_rate(case)))

    before = case.shot("before-start")
    code, body = case.http("/pad?button=start&hold=2.0")
    case.check("Start press delivered to the synthetic pad", code == 200,
               body.decode(errors="replace").strip())
    responded, worst = False, 0.0
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and not responded:
        current = case.shot("after-start")
        worst = png_mean_diff(before, current)
        responded = worst > 8.0
        time.sleep(1.0)
    case.check("the presented frame changed after Start "
               "(mean |delta| %.1f > 8)" % worst, responded)
    (case.dir / "final-input-report.txt").write_text(
        case.get_text("/input?controller=0"))


def case_pad_persisted(case: Case) -> None:
    """A controller matching the STORED controller0 id is adopted for
    Player 1 with no session assignment at all -- the path a real pad takes
    when it appears after start and settings already name it. With
    --boot-continue the run boots through the Continue path first, which is
    the exact flow the controller-after-start report describes."""
    stored_id = "x2-test-pad-0001"
    boot_continue = case.options.boot_continue
    case.prepare_profile([
        "boot.mode=%s" % ("continue" if boot_continue else "normal"),
        "input.assignment_version=2",
        "input.keyboard0.player=0",
        "input.keyboard1.player=unassigned",
        "input.keyboard2.player=unassigned",
        "input.keyboard3.player=unassigned",
        "input.controller0.id=%s" % stored_id,
        "input.controller0.player=0",
        "input.controller1.id=",
        "input.controller1.player=unassigned",
        "input.controller2.id=",
        "input.controller2.player=unassigned",
        "input.controller3.id=",
        "input.controller3.player=unassigned",
    ])
    env = {
        # After the save load on the Continue path: the deserialized input
        # manager is what must adopt the pad, which is the reported scenario.
        "X2_VIRTUAL_PAD": "f4000" if boot_continue else "f600",
        "X2_VIRTUAL_PAD_ID": stored_id,
        "X2_FILES": "1",
        "X2_SCRIPTS": "1",
    }
    if boot_continue:
        case.seed_save("autosave.save")
    else:
        env["X2_BOOT_MAP"] = TUTORIAL_MAP
    case.launch(env)
    case.wait_control(60)
    if boot_continue:
        case.check("Continue boot reached the saved map",
                   case.wait_log("BOOT PLAYER:", 240))
        case.check("destination map opened before driving the pad",
                   case.wait_log("/maps/act0/tutorial/tutorial1.pkgb", 300))
        time.sleep(10)

    case.check("the synthetic identity was announced",
               case.wait_log("X2_VIRTUAL_PAD_ID", 60))
    reached = reach_authored_conversation(case, 240)
    case.check("authored conversation reached (gameplay context)", reached)
    if not reached:
        return
    code, body = case.http("/key?name=Escape&hold=0.4")
    case.check("keyboard Escape cleared the authored scene", code == 200,
               body.decode(errors="replace").strip())
    case.check("controls unlocked after the skip",
               wait_controls_unlocked(case, 180))

    adopted, detail = False, ""
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and not adopted:
        if not case.alive():
            break
        report = case.get_text("/input?controller=0")
        for line in report.splitlines():
            if "resolved players" in line:
                detail = line.strip()
                adopted = stored_id in line and "P1=%s" % stored_id in line
        time.sleep(1.0)
    case.check("player 1 resolved to the stored id with no session "
               "assignment", adopted, detail)
    case.check("the synthetic pad attached before driving",
               case.wait_log("DINPUT-PAD: pad 0 connected", 120))
    case.check("the game is polling the pad again after the load",
               wait_pad_polled(case, 120))

    before = case.shot("before-start")
    code, body = case.http("/pad?button=start&hold=2.0")
    case.check("Start press delivered to the synthetic pad", code == 200,
               body.decode(errors="replace").strip())
    responded, worst = False, 0.0
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and not responded:
        current = case.shot("after-start")
        worst = png_mean_diff(before, current)
        responded = worst > 8.0
        time.sleep(1.0)
    case.check("the presented frame changed after Start "
               "(mean |delta| %.1f > 8)" % worst, responded)


def case_deadzone_render(case: Case) -> None:
    """Code-spawn a Scourge Critter and prove the Dead Zone sea is textured
    and changing. This is deliberately a render observation, not a general
    gameplay gate: the unit/selftests own the renderer contracts."""
    case.prepare_profile(["boot.mode=normal"])
    case.launch({
        "X2_BOOT_MAP": "act1/deadzone/deadzone1",
        "X2_FILES": "1",
        "X2_SCRIPTS": "1",
        "X2_SPAWN_CRITTER": "1",
    })
    case.wait_control(60)
    case.check("the retail spawn path returned a Scourge Critter entity",
               case.wait_log("factory result", 180)
               and "factory result 0x00000000" not in case.log_text())
    case.check("the Scourge Critter model was requested",
               case.wait_log("actors/60_critter.igb", 180))
    case.check("R8G8B8 is no longer refused",
               "CreateTexture in format 20" not in case.log_text())

    # The bounded boot camera leaves a blue sea wedge in the upper-right.
    # Blue-pixel selection below excludes the rocks and foliage crossing it.
    time.sleep(2.0)
    first = case.shot("water-a")
    case.signal(signal.SIGUSR1)
    case.check("a full visible-frame draw table completed",
               case.wait_log("[FRAME TABLE] end of frame", 30))
    time.sleep(1.0)
    second = case.shot("water-b")
    spread, delta = png_water_stats(first, second)
    # This camera exposes only a small, oblique wedge of sea; it cannot be
    # compared numerically with the user's horizon-facing reference crop.
    # It can still distinguish a flat fill from a textured surface, while the
    # GPU pixel selftest owns the mip-chain contract.
    case.check("the visible blue sea is not a flat fill (luma stddev > 1)",
               spread > 1.0,
               "stddev %.2f" % spread)
    case.check("the sea crop changes between frames (mean |delta| > 0.1)",
               delta > 0.1, "mean |delta| %.2f" % delta)


def case_deadzone_water(case: Case) -> None:
    """Measure the Dead Zone sea before a spawned enemy can pull the camera
    inland. The Critter reproduction remains in deadzone-render."""
    case.prepare_profile(["boot.mode=normal"])
    case.launch({
        "X2_BOOT_MAP": "act1/deadzone/deadzone1",
        "X2_FILES": "1",
        "X2_SCRIPTS": "1",
    })
    case.wait_control(60)
    case.check("the Dead Zone map opened",
               case.wait_log("maps/act1/deadzone/deadzone1.igb", 180))
    case.check("the Dead Zone entry script launched",
               case.wait_log('SCRIPT: launch "act1/deadzone/deadzone1/deadzone1"',
                             180))
    time.sleep(3.0)
    first = case.shot("water-a")
    time.sleep(1.0)
    second = case.shot("water-b")
    spread, delta = png_water_stats(first, second)
    case.check("the visible blue sea is not a flat fill (luma stddev > 1)",
               spread > 1.0,
               "stddev %.2f" % spread)
    case.check("the sea crop changes between frames (mean |delta| > 0.1)",
               delta > 0.1, "mean |delta| %.2f" % delta)


def png_water_stats(a: Path, b: Path) -> tuple[float, float]:
    from PIL import Image  # the locked uv environment owns Pillow
    try:
        ia = Image.open(a).convert("RGB")
        ib = Image.open(b).convert("RGB")
    except Exception as exc:
        print("  [WARN] water crop comparison unavailable: %s" % exc)
        return 0.0, 0.0
    if ia.size != ib.size:
        ia = ia.resize(ib.size)
    # The first version hardcoded the equivalent 800x600 box. Issue #135
    # made a fresh profile honor its configured 1280x720 output, exposing that
    # the diagnostic had encoded one resolution rather than the sea region.
    width, height = ib.size
    box = (round(width * 5 / 8), 0, width, round(height * 5 / 12))
    ia = ia.crop(box)
    ib = ib.crop(box)
    pa, pb = list(ia.getdata()), list(ib.getdata())
    # Ignore rocks/foliage in the diagnostic crop. Requiring both frames to
    # classify the pixel as blue also keeps a moving silhouette edge from
    # masquerading as animated water.
    def blue(pixel: tuple[int, int, int]) -> bool:
        red, green, blue_channel = pixel
        return (blue_channel > red * 1.2
                and blue_channel > green * 1.05
                and blue_channel > 50)

    pairs = [(x, y) for x, y in zip(pa, pb, strict=True)
             if blue(x) and blue(y)]
    if len(pairs) < 1000:
        return 0.0, 0.0
    first_blue = [round(0.299 * x[0] + 0.587 * x[1] + 0.114 * x[2])
                  for x, _ in pairs]
    mean = sum(first_blue) / len(first_blue)
    spread = (sum((x - mean) ** 2 for x in first_blue)
              / len(first_blue)) ** 0.5
    delta = sum(abs((0.299 * x[0] + 0.587 * x[1] + 0.114 * x[2])
                    - (0.299 * y[0] + 0.587 * y[1] + 0.114 * y[2]))
                for x, y in pairs) / len(pairs)
    return spread, delta


def case_stick_travel(case: Case) -> None:
    """A thumb steers from where it LANDS, not from where the ring is drawn.

    A thumb does not arrive on the middle of a circle it cannot see. Measured
    from the ring's centre, that landing offset is itself an input: the
    character walks off the moment the screen is touched, in whatever
    direction the thumb happened to land, and full deflection is a short push
    one way against a long reach the other. The user reported it as the stick
    not working right.
    """
    case.prepare_profile(["boot.mode=normal", "input.touch_controls=2"])
    case.launch({"X2_BOOT_MAP": TUTORIAL_MAP})
    case.wait_control(60)
    reached = reach_authored_conversation(case, 240)
    case.check("gameplay reached", reached)
    if not reached:
        return
    case.http("/key?name=Escape&hold=0.4")
    case.check("controls unlocked after the skip",
               wait_controls_unlocked(case, 180))

    # The overlay follows the retail HUD's own decision, which is a heartbeat
    # per drawn frame: it is not up the instant a skip returns control.
    deadline = time.monotonic() + 120
    found = live_stick(case)
    while not found and time.monotonic() < deadline and case.alive():
        time.sleep(2.0)
        found = live_stick(case)
    if not found:
        print("  /controls: %r" % case.get_text("/controls")[:400])
    case.check("the overlay draws its movement ring in gameplay",
               found is not None)
    if not found:
        return
    (left, top, width, height), _ = found
    viewport = live_viewport(case, "/controls")
    case.check("and which surface it is in", viewport is not None)
    if not viewport:
        return
    print("  ring: %g,%g %gx%g in %gx%g" % (left, top, width, height,
                                            viewport[0], viewport[1]))

    def contact(px: float, py: float, phase: str) -> None:
        case.http("/touch?x=%g&y=%g&phase=%s"
                  % (px / viewport[0], py / viewport[1], phase))

    # Where a thumb lands: inside the ring, nowhere near its centre.
    radius = min(width, height) * 0.5
    thumb_x = left + width * 0.5 + radius * 0.45
    thumb_y = top + height * 0.5 + radius * 0.5
    contact(thumb_x, thumb_y, "down")
    time.sleep(0.5)
    rested = live_stick(case)
    print("  deflection on touch-down: %s" % (rested[1] if rested else None,))
    case.check("a thumb that has not moved steers nothing",
               rested is not None and max(abs(v) for v in rested[1]) < 0.01,
               "" if rested is None or max(abs(v) for v in rested[1]) < 0.01
               else "deflected %s before the thumb moved" % (rested[1],))

    # One ring radius of travel from THERE is full deflection, whichever way.
    contact(thumb_x, thumb_y - radius, "motion")
    time.sleep(0.5)
    pushed = live_stick(case)
    print("  deflection after a radius of travel: %s"
          % (pushed[1] if pushed else None,))
    case.check("a radius of travel from where it landed is full deflection",
               pushed is not None and pushed[1][1] < -0.95,
               "" if pushed is None or pushed[1][1] < -0.95
               else "deflected only %s" % (pushed[1],))

    # The same push the other way reaches full too. Measured from the ring
    # this is the short side, and it used to saturate well before this.
    contact(thumb_x, thumb_y, "motion")
    contact(thumb_x - radius, thumb_y, "motion")
    time.sleep(0.5)
    opposite = live_stick(case)
    print("  deflection pushing the other way: %s"
          % (opposite[1] if opposite else None,))
    case.check("and so is the same travel in the opposite direction",
               opposite is not None and opposite[1][0] < -0.95,
               "" if opposite is None or opposite[1][0] < -0.95
               else "deflected only %s" % (opposite[1],))

    contact(thumb_x - radius, thumb_y, "up")
    time.sleep(0.5)
    lifted = live_stick(case)
    case.check("lifting the thumb leaves the ring centred",
               lifted is not None and max(abs(v) for v in lifted[1]) < 0.01,
               "" if lifted is None or max(abs(v) for v in lifted[1]) < 0.01
               else "still deflected %s" % (lifted[1],))

    case.check("and the game is still running",
               case.alive() and case.http("/status")[0] == 200)
