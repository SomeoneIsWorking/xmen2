#!/usr/bin/env python3
"""Non-launching tests for the oracle comparison and drive tools."""

from contextlib import redirect_stderr, redirect_stdout
import io
import os
from pathlib import Path
import tempfile
import unittest

from tools import drive, oracle_compare


class FakeRunner:
    def __init__(
        self,
        results: list[oracle_compare.Result] | None = None,
        log_output: str = "",
    ) -> None:
        self.results = list(results or [])
        self.log_output = log_output
        self.calls: list[tuple[list[str], dict[str, str] | None, Path | None]] = []

    def run(
        self,
        command: list[str] | tuple[str, ...],
        *,
        cwd: Path,
        env: dict[str, str] | None = None,
        output: Path | None = None,
    ) -> oracle_compare.Result:
        del cwd
        self.calls.append(
            (list(command), None if env is None else dict(env), output)
        )
        if output is not None:
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(self.log_output)
        if self.results:
            return self.results.pop(0)
        return oracle_compare.Result(0, "")


class DriveTests(unittest.TestCase):
    def test_profiles_and_native_event_count(self) -> None:
        self.assertEqual(drive.profile("stock"), drive.STOCK_KEYS)
        self.assertEqual(drive.count_events(drive.PORT_SCRIPT), 12)
        reports, errors = drive.validate_profiles(drive.PROFILES)
        self.assertEqual(len(reports), 2)
        self.assertEqual(errors, [])

    def test_profile_validation_rejects_empty_and_malformed_inputs(self) -> None:
        reports, errors = drive.validate_profiles(
            {"port": "", "stock": "195-300/12Return"}
        )
        self.assertEqual(reports, [])
        self.assertEqual(len(errors), 2)
        self.assertIn("EMPTY", errors[0])
        self.assertIn("no ':'", errors[1])

    def test_unknown_drive_command_refuses(self) -> None:
        with redirect_stdout(io.StringIO()):
            self.assertEqual(drive.main(["unknown"]), 2)


class OracleCompareTests(unittest.TestCase):
    def _paths_with_native(self, root: Path) -> oracle_compare.Paths:
        paths = oracle_compare.Paths(root, "stocklog")
        paths.native.parent.mkdir(parents=True)
        paths.native.write_text("fixture")
        paths.native.chmod(0o755)
        return paths

    def test_check_accepts_bound_probe_selftests_without_stock_proxy(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = self._paths_with_native(Path(temporary))
            runner = FakeRunner(
                [
                    oracle_compare.Result(0, "generator detail\ngenerator ok\n"),
                    oracle_compare.Result(0, "generated current\n"),
                    oracle_compare.Result(0, "diff detail\ndiff ok\n"),
                    oracle_compare.Result(0, "7 probes bound to their wrapper\n"),
                    oracle_compare.Result(0, "a\nb\nrecorder ok\n"),
                ]
            )
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(oracle_compare.command_check(paths, runner), 0)
            self.assertIn("generator ok", output.getvalue())
            self.assertIn("NOT BUILT", output.getvalue())

    def test_check_refuses_a_reported_unbound_probe(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = self._paths_with_native(Path(temporary))
            runner = FakeRunner(
                [
                    oracle_compare.Result(0, "generator ok\n"),
                    oracle_compare.Result(0, "generated current\n"),
                    oracle_compare.Result(0, "diff ok\n"),
                    oracle_compare.Result(
                        0,
                        "probe X NOT WRAPPED\n7 probes bound to their wrapper\n",
                    ),
                ]
            )
            with redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(
                    oracle_compare.OracleFailure, "silent gaps"
                ):
                    oracle_compare.command_check(paths, runner)

    def test_port_command_uses_shared_profile_without_launching(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = oracle_compare.Paths(Path(temporary), "stocklog")
            runner = FakeRunner(
                [oracle_compare.Result(0, ""), oracle_compare.Result(124, "")],
                log_output="probe alpha fired 3 time(s)\n",
            )
            with redirect_stdout(io.StringIO()):
                self.assertEqual(
                    oracle_compare.command_port(paths, runner, {"X2_TIMEOUT": "9"}),
                    0,
                )
            command, environment, log_path = runner.calls[1]
            self.assertEqual(command[:2], ["timeout", "9"])
            self.assertEqual(environment["X2_INPUT_SCRIPT"], drive.PORT_SCRIPT)
            self.assertEqual(log_path, paths.root / "scratch/logs/oracle_port.log")

    def test_diff_refuses_when_either_stream_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = oracle_compare.Paths(Path(temporary), "stocklog")
            paths.port_bin.parent.mkdir(parents=True)
            paths.port_bin.write_bytes(b"port")
            with self.assertRaisesRegex(oracle_compare.OracleFailure, "Both sides"):
                oracle_compare.require_streams(paths)

    def test_diff_forwards_arguments_and_exit_status(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = oracle_compare.Paths(Path(temporary), "stocklog")
            paths.port_bin.parent.mkdir(parents=True)
            paths.stock_bin.parent.mkdir(parents=True)
            paths.port_bin.write_bytes(b"port")
            paths.stock_bin.write_bytes(b"stock")
            runner = FakeRunner([oracle_compare.Result(7, "different\n")])
            with redirect_stdout(io.StringIO()):
                result = oracle_compare.command_diff(paths, runner, ["--probe", "4"])
            self.assertEqual(result, 7)
            self.assertEqual(runner.calls[0][0][-2:], ["--probe", "4"])

    def test_unknown_oracle_command_refuses_with_exit_two(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                result = oracle_compare.dispatch(
                    ["unknown"],
                    root=Path(temporary),
                    env=os.environ,
                    runner=FakeRunner(),
                )
            self.assertEqual(result, 2)

    def test_stock_run_environment_refuses_path_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(oracle_compare.OracleFailure, "one name"):
                oracle_compare.dispatch(
                    ["diff"],
                    root=Path(temporary),
                    env={"X2_STOCK_RUN": "../../outside"},
                    runner=FakeRunner(),
                )


if __name__ == "__main__":
    unittest.main()
