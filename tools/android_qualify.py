#!/usr/bin/env python3
"""Collect a real Android performance qualification from the shipping APK.

The game must already be running on the named device. This tool never declares
an APK releasable: it records the device, its control-endpoint telemetry, RSS
and thermal-service observations for the required sustained session. The
operator supplies the scenarios they actually exercised; omitted scenarios or
a short session are refused instead of becoming plausible release evidence.
"""

from __future__ import annotations

import argparse
from collections.abc import Callable, Iterable
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from typing import Any
import urllib.error
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PACKAGE = "com.someoneisworking.xmen2"
DEFAULT_PORT = 8420
MINIMUM_DURATION_SECONDS = 20 * 60
REQUIRED_SCENARIOS = frozenset({
    "cold-setup",
    "picker-return",
    "first-movie",
    "combat",
    "touch-only",
    "suspend-resume",
})


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", help="ADB serial; required when more than one device is attached")
    parser.add_argument("--tier", required=True, choices=("low", "target", "high"))
    parser.add_argument("--duration-seconds", type=int, default=MINIMUM_DURATION_SECONDS)
    parser.add_argument("--every-seconds", type=float, default=10.0)
    parser.add_argument("--startup-ms", type=float, required=True,
                        help="Measured cold-start time for the exercised setup/game launch")
    parser.add_argument("--level-load-ms", type=float, required=True,
                        help="Measured representative-level load time")
    parser.add_argument("--audio-verified", action="store_true",
                        help="Record that audio was heard and correct during this run")
    parser.add_argument("--scenario", action="append", default=[], choices=sorted(REQUIRED_SCENARIOS),
                        help="A required scenario actually exercised during this run; repeat for all six")
    parser.add_argument("--package", default=DEFAULT_PACKAGE)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--output", type=Path,
                        default=ROOT / "scratch/run/android-qualification.json")
    return parser.parse_args()


def run_adb(adb: str, serial: str, arguments: Iterable[str]) -> str:
    command = [adb, "-s", serial, *arguments]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
        raise RuntimeError(f"adb command failed ({' '.join(command)}): {detail}")
    return result.stdout


def connected_devices(adb: str) -> list[str]:
    result = subprocess.run([adb, "devices"], text=True, capture_output=True, check=False)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
        raise RuntimeError(f"adb devices failed: {detail}")
    return [line.split()[0] for line in result.stdout.splitlines()
            if line.split()[1:2] == ["device"]]


def select_device(devices: list[str], requested: str | None) -> str:
    if requested:
        if requested not in devices:
            raise RuntimeError(f"ADB serial {requested!r} is not an attached ready device")
        return requested
    if len(devices) != 1:
        names = ", ".join(devices) or "none"
        raise RuntimeError("expected exactly one ready Android device; use --serial "
                           f"to choose one (found: {names})")
    return devices[0]


def parse_total_pss_kb(text: str) -> int:
    match = re.search(r"^\s*TOTAL\s+PSS:\s*([0-9,]+)", text, re.MULTILINE)
    if not match:
        raise RuntimeError("dumpsys meminfo did not report a TOTAL PSS value")
    return int(match.group(1).replace(",", ""))


def thermal_observations(text: str) -> list[str]:
    observations = [line.strip() for line in text.splitlines()
                    if re.search(r"temperature|status", line, re.IGNORECASE)]
    if not observations:
        raise RuntimeError("dumpsys thermalservice returned no thermal/status observations")
    return observations


def require_scenarios(scenarios: Iterable[str]) -> list[str]:
    supplied = set(scenarios)
    missing = sorted(REQUIRED_SCENARIOS - supplied)
    if missing:
        raise RuntimeError("missing required observed scenarios: " + ", ".join(missing))
    return sorted(supplied)


def control_status(port: int) -> dict[str, Any]:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/status", timeout=15) as response:
            if response.status != 200:
                raise RuntimeError(f"control /status returned HTTP {response.status}")
            payload = json.loads(response.read())
    except urllib.error.URLError as error:
        raise RuntimeError(f"cannot reach forwarded game control port {port}: {error.reason}") from error
    if not isinstance(payload, dict):
        raise RuntimeError("control /status returned a non-object JSON payload")
    fields = ("frames_presented", "renderer_ready", "renderer_backend",
              "presentation_width", "presentation_height", "frame_ms_p50",
              "frame_ms_p95", "frame_ms_p99", "frame_sample_count")
    missing = [field for field in fields if field not in payload]
    if missing:
        raise RuntimeError("game control status lacks qualification fields: " + ", ".join(missing))
    return payload


def reset_frame_timing(port: int) -> None:
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/performance/reset", method="GET"
    )
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            if response.status != 200:
                raise RuntimeError(f"control timing reset returned HTTP {response.status}")
    except urllib.error.HTTPError as error:
        raise RuntimeError(
            f"control timing reset returned HTTP {error.code}: "
            f"{error.read().decode(errors='replace').strip()}"
        ) from error
    except urllib.error.URLError as error:
        raise RuntimeError(f"cannot reach forwarded game control port {port}: {error.reason}") from error


def build_report(device: dict[str, str], tier: str, scenarios: list[str],
                 started: float, samples: list[dict[str, Any]],
                 startup_ms: float, level_load_ms: float, audio_verified: bool) -> dict[str, Any]:
    if len(samples) < 2:
        raise RuntimeError("qualification captured fewer than two status samples")
    if samples[-1]["elapsed_s"] < MINIMUM_DURATION_SECONDS:
        raise RuntimeError("qualification ended before the required sustained duration")
    scenarios = require_scenarios(scenarios)
    if startup_ms <= 0 or level_load_ms <= 0:
        raise RuntimeError("startup and representative level-load measurements must be positive")
    if not audio_verified:
        raise RuntimeError("audio must be manually verified during qualification")
    first = samples[0]["status"]
    last = samples[-1]["status"]
    frames = int(last["frames_presented"]) - int(first["frames_presented"])
    if frames <= 0 or not last["renderer_ready"]:
        raise RuntimeError("the game did not present frames through its renderer during qualification")
    if not last["renderer_backend"] or (int(last["presentation_width"]) <= 0 or
                                         int(last["presentation_height"]) <= 0):
        raise RuntimeError("the game did not report an active renderer backend and resolution")
    if int(last["frame_sample_count"]) <= 0:
        raise RuntimeError("the game reported no frame-time samples")
    memory = [int(sample["total_pss_kb"]) for sample in samples]
    return {
        "schema": 1,
        "kind": "android-performance-qualification",
        "started_unix_s": started,
        "duration_s": samples[-1]["elapsed_s"],
        "device": device,
        "tier": tier,
        "observed_scenarios": scenarios,
        "manual_observations": {
            "cold_start_ms": startup_ms,
            "representative_level_load_ms": level_load_ms,
            "audio_verified": audio_verified,
        },
        "frames_presented_during_run": frames,
        "renderer": {
            "backend": last["renderer_backend"],
            "width": last["presentation_width"],
            "height": last["presentation_height"],
        },
        "final_frame_time_ms": {
            "p50": last["frame_ms_p50"],
            "p95": last["frame_ms_p95"],
            "p99": last["frame_ms_p99"],
            "samples": last["frame_sample_count"],
        },
        "peak_total_pss_kb": max(memory),
        "samples": samples,
        "release_status": "evidence-collected-not-a-release-decision",
    }


def write_report(path: Path, report: dict[str, Any]) -> None:
    path = path.resolve()
    try:
        path.relative_to((ROOT / "scratch").resolve())
    except ValueError:
        raise RuntimeError(f"refusing report path outside scratch/: {path}") from None
    path.parent.mkdir(parents=True, exist_ok=True)
    preparing = path.with_suffix(path.suffix + ".preparing")
    preparing.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    preparing.replace(path)


def device_properties(adb: str, serial: str) -> dict[str, str]:
    names = ("ro.product.manufacturer", "ro.product.model", "ro.product.device",
             "ro.build.fingerprint", "ro.build.version.release", "ro.build.version.sdk")
    return {name: run_adb(adb, serial, ["shell", "getprop", name]).strip() for name in names}


def collect(args: argparse.Namespace, *, monotonic: Callable[[], float] = time.monotonic,
            sleep: Callable[[float], None] = time.sleep) -> dict[str, Any]:
    if args.duration_seconds < MINIMUM_DURATION_SECONDS:
        raise RuntimeError(f"qualification requires at least {MINIMUM_DURATION_SECONDS} seconds")
    if args.every_seconds <= 0:
        raise RuntimeError("--every-seconds must be positive")
    scenarios = require_scenarios(args.scenario)
    serial = select_device(connected_devices(args.adb), args.serial)
    run_adb(args.adb, serial, ["forward", f"tcp:{args.port}", f"tcp:{args.port}"])
    try:
        reset_frame_timing(args.port)
        started_unix_s = time.time()
        started = monotonic()
        deadline = started + args.duration_seconds
        samples: list[dict[str, Any]] = []
        while True:
            status = control_status(args.port)
            meminfo = run_adb(args.adb, serial, ["shell", "dumpsys", "meminfo", args.package])
            thermal = run_adb(args.adb, serial, ["shell", "dumpsys", "thermalservice"])
            samples.append({
                "elapsed_s": round(monotonic() - started, 3),
                "status": status,
                "total_pss_kb": parse_total_pss_kb(meminfo),
                "thermal": thermal_observations(thermal),
            })
            if monotonic() >= deadline:
                break
            sleep(min(args.every_seconds, max(0.0, deadline - monotonic())))
    finally:
        run_adb(args.adb, serial, ["forward", "--remove", f"tcp:{args.port}"])
    report = build_report(device_properties(args.adb, serial), args.tier, scenarios,
                          started_unix_s, samples, args.startup_ms,
                          args.level_load_ms, args.audio_verified)
    write_report(args.output, report)
    return report


def main() -> int:
    try:
        report = collect(parse_args())
    except RuntimeError as error:
        print(f"android qualification: {error}", file=sys.stderr)
        return 2
    print("android qualification: recorded %d frames, p50 %.2f ms, p95 %.2f ms, "
          "p99 %.2f ms; evidence only, APK remains unreleased" % (
              report["frames_presented_during_run"],
              report["final_frame_time_ms"]["p50"],
              report["final_frame_time_ms"]["p95"],
              report["final_frame_time_ms"]["p99"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
