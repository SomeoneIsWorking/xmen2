#!/usr/bin/env python3
"""Record and compare guest-function probes in the port and stock game.

Usage:
    tools/oracle_compare.py check          verify the harness end to end
    tools/oracle_compare.py build          regenerate and build both sides
    tools/oracle_compare.py port           record the native port
    tools/oracle_compare.py stock [secs]   record the Wine control
    tools/oracle_compare.py diff [args]    compare the two streams

The two runs need not be driven identically. Records line up by probe and call
index; mismatched inputs are run drift, while matching inputs with mismatched
outputs isolate a defect in the probed function.
"""

from __future__ import annotations

from dataclasses import dataclass
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Mapping, Sequence


def _load_tool_module(stem: str):
    path = Path(__file__).resolve().with_name(f"{stem}.py")
    spec = importlib.util.spec_from_file_location(f"x2_{stem}", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load driving profiles from {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


drive = _load_tool_module("drive")


TOOL_NAME = "oracle_compare"
MINGW_CC = "i686-w64-mingw32-gcc"


class OracleFailure(RuntimeError):
    """A harness precondition failed, so recording or comparison must refuse."""


@dataclass(frozen=True)
class Result:
    returncode: int
    output: str


class Runner:
    def run(
        self,
        command: Sequence[str],
        *,
        cwd: Path,
        env: Mapping[str, str] | None = None,
        output: Path | None = None,
    ) -> Result:
        try:
            if output is None:
                completed = subprocess.run(
                    list(command),
                    cwd=cwd,
                    env=None if env is None else dict(env),
                    check=False,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                )
                return Result(completed.returncode, completed.stdout or "")

            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open("w") as stream:
                completed = subprocess.run(
                    list(command),
                    cwd=cwd,
                    env=None if env is None else dict(env),
                    check=False,
                    text=True,
                    stdout=stream,
                    stderr=subprocess.STDOUT,
                )
            return Result(completed.returncode, "")
        except FileNotFoundError:
            message = f"{command[0]} not found\n"
            if output is not None:
                output.write_text(message)
                return Result(127, "")
            return Result(127, message)


@dataclass(frozen=True)
class Paths:
    root: Path
    stock_run: str

    @property
    def port_bin(self) -> Path:
        return self.root / "scratch/logs/probe_port.bin"

    @property
    def stock_bin(self) -> Path:
        return self.root / "scratch/run" / self.stock_run / "probe_stock.bin"

    @property
    def native(self) -> Path:
        return self.root / "scratch/build-native/x2native"

    @property
    def proxy(self) -> Path:
        return self.root / "scratch/build-proxy/d3d8.dll"


def _python(root: Path, tool: str, *arguments: str) -> list[str]:
    return [sys.executable, str(root / "tools" / tool), *arguments]


def _run_required(
    runner: Runner,
    command: Sequence[str],
    *,
    root: Path,
    failure: str,
) -> Result:
    result = runner.run(command, cwd=root)
    if result.returncode != 0:
        if result.output:
            sys.stdout.write(result.output)
        raise OracleFailure(failure)
    return result


def _last_lines(output: str, count: int) -> str:
    lines = output.splitlines()
    return "\n".join(lines[-count:])


def _matching_lines(output: str, needles: Sequence[str], limit: int | None = None) -> list[str]:
    matches = [line for line in output.splitlines() if any(item in line for item in needles)]
    return matches if limit is None else matches[:limit]


def regenerate(paths: Paths, runner: Runner) -> None:
    _run_required(
        runner,
        _python(paths.root, "gen_probes.py"),
        root=paths.root,
        failure="tools/gen_probes.py refused; NOTHING was regenerated",
    )


def command_check(paths: Paths, runner: Runner) -> int:
    print("== manifest and generated artifacts ==")
    generated_test = _run_required(
        runner,
        _python(paths.root, "gen_probes.py", "--selftest"),
        root=paths.root,
        failure="tools/gen_probes.py selftest FAILED",
    )
    print(_last_lines(generated_test.output, 1))
    _run_required(
        runner,
        _python(paths.root, "gen_probes.py", "--check"),
        root=paths.root,
        failure="the generated artifacts are stale -- run tools/gen_probes.py",
    )
    diff_test = _run_required(
        runner,
        _python(paths.root, "oraclediff.py", "--selftest"),
        root=paths.root,
        failure="tools/oraclediff.py selftest FAILED",
    )
    print(_last_lines(diff_test.output, 1))

    print("\n== the port's hooks actually bound ==")
    if not paths.native.is_file() or not os.access(paths.native, os.X_OK):
        raise OracleFailure(
            f"{paths.native} does not exist; run 'tools/oracle_compare.py build'"
        )
    binding = runner.run(
        [str(paths.native), "--no-window", "--probe-selftest"], cwd=paths.root
    )
    sys.stdout.write(binding.output)
    bad_binding = _matching_lines(
        binding.output, ("NOT WRAPPED", "IS NOT LINKED", "no entry")
    )
    if bad_binding:
        raise OracleFailure(
            "at least one probe did not bind; capturing now would produce\n"
            "  a stream with silent gaps, which compares as agreement"
        )
    if "bound to their wrapper" not in binding.output:
        raise OracleFailure("the binding check did not run at all")

    print("\n== the recorder writes what the guest holds ==")
    recorder = _run_required(
        runner,
        [str(paths.native), "--no-window", "--probe-selftest"],
        root=paths.root,
        failure="the probe recorder selftest FAILED",
    )
    tail = _last_lines(recorder.output, 3)
    if tail:
        print(tail)

    print("\n== the stock side ==")
    if not paths.proxy.is_file():
        print("  NOT BUILT -- run 'tools/oracle_compare.py build' (needs mingw32 + a Wine prefix)")
        return 0
    print("  built: scratch/build-proxy/d3d8.dll")
    nm = runner.run(["i686-w64-mingw32-nm", str(paths.proxy)], cwd=paths.root)
    if nm.returncode == 0 and "probe_stub_0" in nm.output:
        print("  ok    the probe stubs are linked into it")
    else:
        print("  NOTE  no probe_stub symbols; rebuild with 'tools/oracle_compare.py build'")
    return 0


def isolate_modules() -> list[str]:
    gen_probes = _load_tool_module("gen_probes")
    return sorted({module for module, _ in gen_probes.isolate_eps()})


def command_build(paths: Paths, runner: Runner) -> int:
    regenerate(paths, runner)
    print("== re-emitting the probed modules so --wrap can bind ==")
    for module in isolate_modules():
        source_json = paths.root / "scratch/recomp" / f"{module}.json"
        if not source_json.is_file():
            raise OracleFailure(
                f"{source_json} is missing; run tools/ghidra_export.sh {module}"
            )
        print(f"  {module}")
        _run_required(
            runner,
            _python(
                paths.root,
                "recomp.py",
                "emit",
                str(source_json),
                str(paths.root / "src/recomp" / f"{module}.c"),
                "--split",
                "1500",
                "--isolate",
                str(paths.root / "scratch/recomp" / f"{module}.isolate"),
            ),
            root=paths.root,
            failure=f"re-emitting {module} FAILED",
        )

    _run_required(
        runner,
        [
            "cmake",
            "-S",
            str(paths.root),
            "-B",
            str(paths.root / "scratch/build-native"),
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        ],
        root=paths.root,
        failure="cmake configure FAILED",
    )
    _run_required(
        runner,
        [
            "cmake",
            "--build",
            str(paths.root / "scratch/build-native"),
            "--target",
            "x2native",
            f"-j{os.cpu_count() or 1}",
        ],
        root=paths.root,
        failure="building x2native FAILED",
    )
    print(f"  built {paths.native}")
    if shutil.which(MINGW_CC) is None:
        print(f"  SKIPPED the stock side: {MINGW_CC} is not installed.")
        print("  Only the port can be recorded, and one stream compares to nothing.")
        return 0
    stock_build = _run_required(
        runner,
        _python(paths.root, "build_stocklog.py", paths.stock_run),
        root=paths.root,
        failure="building the stock proxy FAILED",
    )
    sys.stdout.write(stock_build.output)
    return 0


def _unlink_owned(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        raise OracleFailure(f"refusing to replace directory at recording path: {path}")


def command_port(paths: Paths, runner: Runner, env: Mapping[str, str]) -> int:
    regenerate(paths, runner)
    paths.port_bin.parent.mkdir(parents=True, exist_ok=True)
    _unlink_owned(paths.port_bin)
    log = paths.root / "scratch/logs/oracle_port.log"
    port_profile = drive.profile("port")
    print(f"{TOOL_NAME}: recording the port to {paths.port_bin}, DRIVEN by")
    print(f"  {port_profile}")
    child_env = dict(env)
    child_env.update(
        {
            "X2_PROBE": str(paths.port_bin),
            "X2_INPUT_SCRIPT": port_profile,
            "X2_MAX_FRAMES": env.get("X2_MAX_FRAMES", "4200"),
            "X2_UNPACED": "1",
            "X2_HEARTBEAT": "60",
        }
    )
    result = runner.run(
        [
            "timeout",
            env.get("X2_TIMEOUT", "420"),
            str(paths.native),
            "--no-window",
            "--d3d8",
            "--run",
        ],
        cwd=paths.root,
        env=child_env,
        output=log,
    )
    size = paths.port_bin.stat().st_size if paths.port_bin.is_file() else 0
    print(f"{TOOL_NAME}: exit {result.returncode}, {size} byte(s) recorded")
    log_text = log.read_text() if log.is_file() else ""
    for line in _matching_lines(log_text, ("oracle_trace", "INJECTING"), 20):
        print(line)
    fired = _matching_lines(log_text, ("probe ",))
    fired = [line for line in fired if " fired" in line]
    if fired:
        print(fired[-1])
        return 0
    return 1


def command_stock(
    paths: Paths,
    runner: Runner,
    seconds: str,
    env: Mapping[str, str],
) -> int:
    staged_proxy = paths.root / "scratch/run" / paths.stock_run / "d3d8.dll"
    if not staged_proxy.is_file():
        raise OracleFailure(
            f"scratch/run/{paths.stock_run} is not staged; "
            "run 'tools/oracle_compare.py build'"
        )
    _unlink_owned(paths.stock_bin)
    stock_profile = drive.profile("stock")
    print(f"{TOOL_NAME}: the control is DRIVEN by {stock_profile}")
    child_env = dict(env)
    child_env["X2_KEYS"] = stock_profile
    stock_run = runner.run(
        [str(paths.root / "tools/run_shim.sh"), paths.stock_run, seconds],
        cwd=paths.root,
        env=child_env,
    )
    sys.stdout.write(stock_run.output)
    size = paths.stock_bin.stat().st_size if paths.stock_bin.is_file() else 0
    print(f"{TOOL_NAME}: {size} byte(s) recorded")
    stock_log = paths.root / "scratch/run" / paths.stock_run / "probe_stock.log"
    if stock_log.is_file():
        for line in stock_log.read_text().splitlines()[:40]:
            print(line)
    return 0


def require_streams(paths: Paths) -> None:
    missing = [path for path in (paths.port_bin, paths.stock_bin) if not path.is_file()]
    if missing:
        raise OracleFailure(
            f"{missing[0]} does not exist. Both sides must have been\n"
            "  recorded; one stream compares to nothing. See "
            "'tools/oracle_compare.py port' and 'tools/oracle_compare.py stock'."
        )


def command_diff(paths: Paths, runner: Runner, arguments: Sequence[str]) -> int:
    require_streams(paths)
    result = runner.run(
        _python(
            paths.root,
            "oraclediff.py",
            str(paths.port_bin),
            str(paths.stock_bin),
            *arguments,
        ),
        cwd=paths.root,
    )
    sys.stdout.write(result.output)
    return result.returncode


def usage() -> str:
    return "\n".join((__doc__ or "").strip().splitlines()[2:8])


def validated_stock_run(run_name: str) -> str:
    build_stocklog = _load_tool_module("build_stocklog")
    try:
        build_stocklog.validate_run_name(run_name)
    except build_stocklog.BuildRefusal as exc:
        raise OracleFailure(str(exc)) from exc
    return run_name


def dispatch(
    argv: Sequence[str],
    *,
    root: Path,
    env: Mapping[str, str],
    runner: Runner,
) -> int:
    command = argv[0] if argv else "check"
    arguments = list(argv[1:])
    stock_run = validated_stock_run(env.get("X2_STOCK_RUN", "stocklog"))
    paths = Paths(root, stock_run)
    if command == "check":
        return command_check(paths, runner)
    if command == "build":
        return command_build(paths, runner)
    if command == "port":
        return command_port(paths, runner, env)
    if command == "stock":
        return command_stock(paths, runner, arguments[0] if arguments else "540", env)
    if command == "diff":
        return command_diff(paths, runner, arguments)
    print(usage())
    return 2


def main(argv: Sequence[str] | None = None) -> int:
    root = Path(__file__).resolve().parent.parent
    try:
        return dispatch(
            list(sys.argv[1:] if argv is None else argv),
            root=root,
            env=os.environ,
            runner=Runner(),
        )
    except OracleFailure as exc:
        print(f"{TOOL_NAME}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
