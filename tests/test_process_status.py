from __future__ import annotations

import os

import pytest

from tools import process_status


def test_windows_probe_never_uses_os_kill(monkeypatch):
    observed: list[int] = []

    def forbidden_kill(pid: int, signal_number: int) -> None:
        raise AssertionError(f"os.kill({pid}, {signal_number}) would signal the Windows console")

    def alive_probe(pid: int) -> bool:
        observed.append(pid)
        return True

    monkeypatch.setattr(process_status.platform, "system", lambda: "Windows")
    monkeypatch.setattr(process_status.os, "kill", forbidden_kill)

    assert process_status.pid_is_alive("42", windows_probe=alive_probe)
    assert observed == [42]


def test_windows_probe_reports_missing_and_propagates_query_failures(monkeypatch):
    monkeypatch.setattr(process_status.platform, "system", lambda: "Windows")

    assert not process_status.pid_is_alive(42, windows_probe=lambda _pid: False)

    def failed_probe(_pid: int) -> bool:
        raise OSError("process query failed")

    with pytest.raises(OSError, match="process query failed"):
        process_status.pid_is_alive(42, windows_probe=failed_probe)


def test_posix_probe_distinguishes_missing_and_permission_denied(monkeypatch):
    monkeypatch.setattr(process_status.platform, "system", lambda: "Linux")

    def missing(_pid: int, _signal_number: int) -> None:
        raise ProcessLookupError

    monkeypatch.setattr(process_status.os, "kill", missing)
    assert not process_status.pid_is_alive(42)

    def inaccessible(_pid: int, _signal_number: int) -> None:
        raise PermissionError

    monkeypatch.setattr(process_status.os, "kill", inaccessible)
    assert process_status.pid_is_alive(42)

    def failed(_pid: int, _signal_number: int) -> None:
        raise OSError("process query failed")

    monkeypatch.setattr(process_status.os, "kill", failed)
    with pytest.raises(OSError, match="process query failed"):
        process_status.pid_is_alive(42)


def test_invalid_pid_values_are_rejected_without_probing(monkeypatch):
    def unexpected_probe(_pid: int, _signal_number: int) -> None:
        raise AssertionError("invalid PID reached the platform probe")

    monkeypatch.setattr(process_status.os, "kill", unexpected_probe)

    for value in (None, False, 0, -1, "not-a-pid"):
        assert not process_status.pid_is_alive(value)


def test_current_posix_process_is_alive():
    if process_status.platform.system() != "Windows":
        assert process_status.pid_is_alive(os.getpid())
