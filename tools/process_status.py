"""Portable process-liveness checks without signaling the target on Windows."""

from __future__ import annotations

from collections.abc import Callable
import os
import platform


WindowsProbe = Callable[[int], bool]


def _windows_pid_is_alive(pid: int) -> bool:
    # On Windows, os.kill(pid, 0) means CTRL_C_EVENT rather than a signal-free
    # existence check. Query the process handle and wait state instead.
    import ctypes
    from ctypes import wintypes

    synchronize = 0x00100000
    wait_object_0 = 0x00000000
    wait_timeout = 0x00000102
    error_access_denied = 5
    error_invalid_parameter = 87

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    open_process = kernel32.OpenProcess
    open_process.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    open_process.restype = wintypes.HANDLE
    wait_for_single_object = kernel32.WaitForSingleObject
    wait_for_single_object.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    wait_for_single_object.restype = wintypes.DWORD
    close_handle = kernel32.CloseHandle
    close_handle.argtypes = (wintypes.HANDLE,)
    close_handle.restype = wintypes.BOOL

    handle = open_process(synchronize, False, pid)
    if not handle:
        error = ctypes.get_last_error()
        if error == error_access_denied:
            return True
        if error == error_invalid_parameter:
            return False
        raise OSError(error, ctypes.FormatError(error))

    try:
        state = wait_for_single_object(handle, 0)
    finally:
        close_handle(handle)
    if state == wait_timeout:
        return True
    if state == wait_object_0:
        return False
    error = ctypes.get_last_error()
    raise OSError(error, ctypes.FormatError(error))


def pid_is_alive(value: object, *, windows_probe: WindowsProbe | None = None) -> bool:
    if isinstance(value, bool):
        return False
    try:
        pid = int(value)
    except (TypeError, ValueError):
        return False
    if pid <= 0:
        return False

    if platform.system() == "Windows":
        probe = windows_probe or _windows_pid_is_alive
        return probe(pid)

    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True
