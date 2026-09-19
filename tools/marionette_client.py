#!/usr/bin/env python3
"""A minimal Marionette client, for driving a Firefox-family browser.

Chrome-family browsers are driven over CDP by ``tools/cdp_client.py``. Firefox
and its derivatives (Zen) speak Marionette instead, and the two protocols share
nothing, so this is a separate owner rather than a branch inside the CDP client.

The transport is length-prefixed JSON over a loopback socket: ``<len>:<json>``,
where the payload is ``[type, message_id, name_or_error, parameters_or_result]``.
"""

from __future__ import annotations

import json
import socket
from typing import Any, ClassVar


class MarionetteError(RuntimeError):
    """The browser refused a command, or the transport broke."""


class Marionette:
    """One Marionette session against an already-running browser."""

    def __init__(self, port: int = 2828, host: str = "127.0.0.1", timeout: float = 120.0) -> None:
        self._socket = socket.create_connection((host, port), timeout=timeout)
        self._socket.settimeout(timeout)
        self._buffer = b""
        self._next_id = 1
        self.handshake = self._read()
        self.session = self.command("WebDriver:NewSession", {})

    def close(self) -> None:
        try:
            self._socket.close()
        except OSError:
            pass

    def __enter__(self) -> "Marionette":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def command(self, name: str, parameters: dict[str, Any] | None = None) -> Any:
        message_id = self._next_id
        self._next_id += 1
        self._send([0, message_id, name, parameters or {}])
        while True:
            message = self._read()
            if not isinstance(message, list) or len(message) != 4:
                raise MarionetteError(f"unexpected Marionette frame: {message!r}")
            _kind, reply_id, error, result = message
            if reply_id != message_id:
                continue
            if error is not None:
                raise MarionetteError(f"{name}: {error}")
            return result

    def navigate(self, url: str) -> None:
        self.command("WebDriver:Navigate", {"url": url})

    def script(self, body: str, timeout_ms: int = 60000, *args: Any) -> Any:
        """Run an async script; the page calls ``resolve(value)`` when done.

        Extra positional arguments reach the script as ``arguments[0..]``,
        ahead of the resolve callback the page calls last.
        """
        self.command("WebDriver:SetTimeouts", {"script": timeout_ms})
        return self.command(
            "WebDriver:ExecuteAsyncScript",
            {"script": body, "args": list(args), "newSandbox": False},
        )

    #: The WebDriver key names this client needs, by their spec code points.
    KEYS: ClassVar[dict[str, str]] = {
        "Enter": "\ue007",
        "Escape": "\ue00c",
        "Space": " ",
        "Up": "\ue013",
        "Down": "\ue015",
        "Left": "\ue012",
        "Right": "\ue014",
    }

    def press(self, key: str, hold_ms: int = 120) -> None:
        """One key down/up through the browser's own input pipeline.

        Not a synthesized DOM event from page script: a game listening through
        SDL wants the events the browser itself dispatches, and a script-made
        event is distinguishable and can be ignored. ``key`` is a name from
        :data:`KEYS` or a literal character.
        """
        value = self.KEYS.get(key, key)
        self.command(
            "WebDriver:PerformActions",
            {
                "actions": [
                    {
                        "type": "key",
                        "id": "keyboard",
                        "actions": [
                            {"type": "keyDown", "value": value},
                            {"type": "pause", "duration": hold_ms},
                            {"type": "keyUp", "value": value},
                        ],
                    }
                ]
            },
        )

    def screenshot(self) -> str:
        return self.command("WebDriver:TakeScreenshot", {"full": True, "hash": False})

    def _send(self, message: list[Any]) -> None:
        payload = json.dumps(message).encode()
        self._socket.sendall(b"%d:%s" % (len(payload), payload))

    def _read(self) -> Any:
        while b":" not in self._buffer:
            self._fill()
        head, _, rest = self._buffer.partition(b":")
        length = int(head)
        self._buffer = rest
        while len(self._buffer) < length:
            self._fill()
        payload, self._buffer = self._buffer[:length], self._buffer[length:]
        return json.loads(payload)

    def _fill(self) -> None:
        chunk = self._socket.recv(65536)
        if not chunk:
            raise MarionetteError("the browser closed the Marionette connection")
        self._buffer += chunk
