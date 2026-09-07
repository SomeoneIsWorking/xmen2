#!/usr/bin/env python3
"""Measure what of the engine compiles to wasm32, and refuse to guess.

The web target (`docs/web-release.md`) is blocked on things that do not exist,
and the honest question while it is blocked is not "does the web build work" --
it does not -- but "which parts of the engine are already portable, and which
exact files are not, and why". A number without a denominator cannot answer
that, and neither can a green tick on a job that compiled nothing.

So this configures the real CMake projects with Emscripten, builds them with
keep-going, and reports built/total per component. Every file that fails is
named with the reason it fails. The measurement is pinned: a file that used to
compile and stops is a regression, and a file that starts compiling is progress
that has to be recorded here before it counts, exactly like the structure
ratchet.

Exit codes: 0 measured and matched the pin, 1 measured and did not, 77 the
toolchain is absent (CTest's skip) unless --require-emsdk says its absence is
itself the failure.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import shared_dir

SKIP = 77

# What is known NOT to compile to wasm32, and why. These are the two blockers
# W1 and a consequence of them, expressed as compile errors rather than as
# prose -- which is the point: the pin makes the claim falsifiable.
#
# An entry removed from here must be removed because the file now compiles.
KNOWN_UNPORTABLE = {
    "code_memory.cpp": (
        "llvm.clear_cache is not supported on wasm. This is the JIT's "
        "executable code region: WebAssembly has no instruction cache to "
        "flush because it has no way to execute a byte buffer at all. Gate W1."
    ),
    "x87.c": (
        "'#pragma FENV_ACCESS' is not supported on this target. WebAssembly "
        "has no floating-point environment -- no rounding-mode control and no "
        "exception flags -- so a wasm build cannot delegate the guest x87 "
        "control word to host FP and must route x87 through the software float "
        "path instead. The Bochs softfloat sources themselves compile clean."
    ),
}

# Port owners with no window, no device and no external dependency. They are
# measured one at a time because they have no wasm CMake project yet; the point
# is to know that the platform-neutral half of the host is already portable.
PORT_OWNERS = (
    "src/presentation/touch_layout.c",
    "src/presentation/hud_layout.c",
    "src/config/boot_mode.c",
    "src/config/settings.c",
    "src/config/hud_settings.c",
    "src/native/boot_mode_policy.c",
    "src/input/gameplay_control.c",
    "src/save/save_directory.c",
)
PORT_OWNER_INCLUDES = (
    "src",
    "src/presentation",
    "src/config",
    "src/native",
    "src/input",
)


def emsdk_environment(emsdk: Path | None) -> dict[str, str] | None:
    """The environment `emcmake`/`emcc` need, or None when unavailable."""
    if emsdk is None:
        return os.environ.copy() if shutil.which("emcc") else None
    script = emsdk / "emsdk_env.sh"
    if not script.is_file():
        return None
    dumped = subprocess.run(
        ["bash", "-c", f'source "{script}" >/dev/null 2>&1 && env -0'],
        capture_output=True,
        text=True,
        check=False,
    )
    if dumped.returncode != 0:
        return None
    environment = {}
    for entry in dumped.stdout.split("\0"):
        key, _, value = entry.partition("=")
        if key:
            environment[key] = value
    return environment if environment else None


def run(command: list[str], environment: dict[str, str], cwd: Path | None = None):
    return subprocess.run(
        command,
        env=environment,
        cwd=cwd,
        capture_output=True,
        text=True,
        check=False,
    )


def ninja_objects(build: Path, environment: dict[str, str], target_dir: str) -> int:
    """How many object files a target has, from ninja's own target list."""
    listed = run(["ninja", "-C", str(build), "-t", "targets", "all"], environment)
    pattern = re.compile(rf"{re.escape(target_dir)}/[^\s:]+\.(?:c|cpp|cc)\.o:")
    return len({line.split(":")[0] for line in listed.stdout.splitlines() if pattern.search(line)})


def built_objects(build: Path, target_dir: str) -> int:
    root = build / target_dir
    return len(list(root.rglob("*.o"))) if root.is_dir() else 0


def measure_engine(
    root: Path, environment: dict[str, str], build: Path
) -> tuple[list[str], list[str]]:
    """Configure and build shared/x86port for wasm32. Returns (report, problems)."""
    report: list[str] = []
    problems: list[str] = []
    x86port = Path(shared_dir.shared_dir("x86port", "CMakeLists.txt"))
    configure = run(
        [
            "emcmake",
            "cmake",
            "-S",
            str(x86port),
            "-B",
            str(build),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            # Zycore knows Emscripten but deliberately gives it no POSIX layer,
            # so its process/memory/terminal/thread sources refuse to compile.
            # The decoder needs none of them.
            "-DZYAN_NO_LIBC=ON",
            # x86port now REFUSES a host it has no JIT backend for, rather than
            # silently linking the x86-64 emitter as it used to. That refusal is
            # correct and is gate W1 restated at configure time. This flag is
            # the named exception for exactly this measurement, and x86port
            # warns on every configure that the library it produces cannot
            # execute a guest instruction. Nothing a person runs may set it.
            "-DX86P_MEASURE_UNRUNNABLE_BACKEND=ON",
        ],
        environment,
    )
    if configure.returncode != 0:
        problems.append(
            "wasm: emcmake could not configure shared/x86port:\n" + configure.stderr.strip()[-2000:]
        )
        return report, problems

    build_result = run(["ninja", "-C", str(build), "-k", "0", "x86port_runtime"], environment)
    failures = {}
    for line in (build_result.stdout + build_result.stderr).splitlines():
        found = re.search(r"^FAILED:.*/([A-Za-z0-9_]+\.(?:c|cpp|cc))\.o\b", line)
        if found:
            failures[found.group(1)] = True

    # Components in dependency order. The directory is how ninja names the
    # target's objects; a component whose directory is absent means the pin is
    # measuring something that no longer exists.
    components = {
        "x86port_runtime (x86 decode, semantics, x87, SIMD, host emission)": "CMakeFiles/x86port_runtime.dir",
        "jitcommon (shared code region and block cache)": "jitcommon/CMakeFiles/jitcommon.dir",
        "Zydis (the pinned decoder)": "vendor/zydis/CMakeFiles/Zydis.dir",
        "Zycore (Zydis support, no-libc)": "vendor/zydis/zycore/CMakeFiles/Zycore.dir",
        "x86p_softfloat (Bochs software x87/SSE math)": "CMakeFiles/x86p_softfloat.dir",
    }
    for label, directory in components.items():
        total = ninja_objects(build, environment, directory)
        done = built_objects(build, directory)
        if total == 0:
            problems.append(
                f"wasm: component {label!r} has 0 objects in the wasm build. "
                f"Either its ninja directory moved ({directory}) or it is no "
                f"longer built -- a pass measuring nothing proves nothing."
            )
            continue
        expected_failures = sorted(
            name
            for name in KNOWN_UNPORTABLE
            if f"{name}.o" in _object_names(build, environment, directory)
        )
        report.append(
            f"  {label}: {done} of {total} translation units compile to wasm32"
            + (f" (unportable: {', '.join(expected_failures)})" if expected_failures else "")
        )

    unexpected = sorted(set(failures) - set(KNOWN_UNPORTABLE))
    if unexpected:
        problems.append(
            "wasm: these compiled to wasm32 before and do not now: "
            + ", ".join(unexpected)
            + ". Either the change is wrong for the web target, or the reason "
            "belongs in KNOWN_UNPORTABLE with the error that explains it."
        )
    recovered = sorted(set(KNOWN_UNPORTABLE) - set(failures))
    if recovered:
        report.append("  PROGRESS: " + ", ".join(recovered) + " now compile to wasm32")
        problems.append(
            "wasm: "
            + ", ".join(recovered)
            + " now compile to wasm32. That is progress and it has to be "
            "recorded: drop them from KNOWN_UNPORTABLE and say so in "
            "docs/web-release.md, so the next regression is visible."
        )
    return report, problems


def _object_names(build: Path, environment: dict[str, str], directory: str) -> set[str]:
    listed = run(["ninja", "-C", str(build), "-t", "targets", "all"], environment)
    return {
        Path(line.split(":")[0]).name
        for line in listed.stdout.splitlines()
        if line.startswith(directory)
    }


def measure_port_owners(
    root: Path, environment: dict[str, str], scratch: Path
) -> tuple[list[str], list[str]]:
    report: list[str] = []
    problems: list[str] = []
    includes = [f"-I{root / path}" for path in PORT_OWNER_INCLUDES]
    compiled = 0
    for name in PORT_OWNERS:
        source = root / name
        if not source.is_file():
            problems.append(
                f"wasm: {name} is pinned as a platform-neutral owner but is "
                f"not present. Update the list rather than losing coverage."
            )
            continue
        result = run(
            ["emcc", "-c", str(source), "-o", str(scratch / "probe.o"), "-O1", *includes, "-w"],
            environment,
        )
        if result.returncode == 0:
            compiled += 1
        else:
            problems.append(
                f"wasm: {name} no longer compiles to wasm32:\n" + result.stderr.strip()[-800:]
            )
    if not PORT_OWNERS:
        problems.append("wasm: the port-owner list is empty; this measures nothing.")
    report.append(
        f"  platform-neutral port owners: {compiled} of {len(PORT_OWNERS)} compile to wasm32"
    )
    return report, problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emsdk", type=Path, default=None, help="an emsdk checkout to source")
    parser.add_argument(
        "--require-emsdk",
        action="store_true",
        help="treat a missing toolchain as a failure rather than a skip (CI)",
    )
    parser.add_argument("--build-dir", type=Path, default=Path("build/wasm/x86port"))
    arguments = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    default_emsdk = root / "build" / "deps" / "emsdk"
    # Every candidate is named in the refusal: "no toolchain" must not be
    # indistinguishable from "looked in the wrong place".
    tried = [str(arguments.emsdk)] if arguments.emsdk else []
    if not arguments.emsdk:
        tried.append(str(default_emsdk))
    tried.append("emcc on PATH")
    emsdk = arguments.emsdk or (default_emsdk if default_emsdk.is_dir() else None)
    environment = emsdk_environment(emsdk)
    if environment is None or not shutil.which("emcc", path=environment.get("PATH", "")):
        message = (
            "wasm_portability: no Emscripten toolchain. Tried:\n"
            + "".join(f"    {candidate}\n" for candidate in tried)
            + "This check measures nothing without one, so it refuses to "
            "report a result."
        )
        if arguments.require_emsdk:
            print(message, file=sys.stderr)
            return 1
        print(f"{message} SKIP.")
        return SKIP

    build = (root / arguments.build_dir).resolve()
    build.mkdir(parents=True, exist_ok=True)
    report, problems = measure_engine(root, environment, build)
    owner_report, owner_problems = measure_port_owners(root, environment, build)
    report += owner_report
    problems += owner_problems

    print("wasm_portability: what compiles to wasm32 today")
    for line in report:
        print(line)
    if not report:
        print(
            "wasm_portability: measured nothing at all -- refusing to report a pass",
            file=sys.stderr,
        )
        return 1
    print(
        "  NOT a claim that the web build works: it does not. "
        "docs/project-state.md S021 is blocked on W1-W3."
    )
    for name, reason in sorted(KNOWN_UNPORTABLE.items()):
        print(f"  unportable, by design for now -- {name}: {reason}")
    if problems:
        print(file=sys.stderr)
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
