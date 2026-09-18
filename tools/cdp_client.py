"""A dependency-free Chrome DevTools Protocol client.

WHY THIS EXISTS. The browser product's guest CPU runs on a pthread worker, and
nothing the page can be asked from JavaScript sees inside it: `weblua-ctl eval`
evaluates on the page, the port's own HTTP control channel cannot be bound in a
browser, and the heartbeat's wall-time split only attributes time inside nested
dispatch spans -- the outermost span never closes, so most of a browser
interval is unattributed by construction. A CPU profile taken by the engine
itself is the one measurement that covers the whole interval with a
denominator.

WHAT IT IS. The smallest client that can reach `Profiler.*` on a worker target:
an RFC 6455 text-frame client over a plain socket plus CDP request/response
correlation. It deliberately depends on nothing outside the standard library,
because the locked tool environment carries no WebSocket package and a
diagnostic must not need one.

WHAT IT IS NOT. Not a browser automation layer -- WebLua owns launching and
driving Chrome. This attaches to a Chrome that is already running and reads
from it.
"""

from __future__ import annotations

import base64
import json
import os
import secrets
import socket
import struct
import urllib.request

_TEXT = 0x1
_BINARY = 0x2
_CLOSE = 0x8
_PING = 0x9
_PONG = 0xA


class CdpError(RuntimeError):
    """A CDP call that the browser refused, or a transport that failed."""


class CdpTimeout(CdpError):
    """A call the target never answered.

    Distinct from CdpError on purpose: a worker parked in `Atomics.wait` cannot
    service the inspector, so "no answer" is a fact about that thread, not a
    broken connection, and a caller iterating over a worker pool must be able
    to tell the two apart and say which threads were unreachable.
    """


def devtools_port(profile_dir: str) -> int:
    """The debugging port Chrome wrote into its profile.

    Rod launches Chrome with `--remote-debugging-port=0`, so the port is only
    discoverable from `DevToolsActivePort` inside the profile it was given.
    Refuses by naming the path rather than returning a guess.
    """
    path = os.path.join(profile_dir, "DevToolsActivePort")
    try:
        with open(path, "r", encoding="utf-8") as handle:
            first = handle.readline().strip()
    except OSError as failure:
        raise CdpError(f"no DevTools port at {path}: {failure}") from failure
    if not first.isdigit():
        raise CdpError(f"{path} does not start with a port number: {first!r}")
    return int(first)


def targets(port: int) -> list[dict]:
    """Every target the browser lists, including workers.

    `/json/list` omits dedicated workers on some Chrome builds, so a caller
    that finds no worker here should fall back to `Target.getTargets` over the
    browser endpoint; `worker_targets` does exactly that.
    """
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=10) as response:
        return json.loads(response.read().decode("utf-8"))


def browser_endpoint(port: int) -> str:
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/version", timeout=10) as response:
        version = json.loads(response.read().decode("utf-8"))
    endpoint = version.get("webSocketDebuggerUrl")
    if not endpoint:
        raise CdpError("the browser reported no webSocketDebuggerUrl")
    return endpoint


class WebSocket:
    """A client-side RFC 6455 connection carrying JSON text frames."""

    def __init__(self, url: str, timeout: float = 30.0) -> None:
        if not url.startswith("ws://"):
            raise CdpError(f"only ws:// is supported, got {url!r}")
        rest = url[len("ws://"):]
        hostport, _, path = rest.partition("/")
        host, _, port = hostport.partition(":")
        self._socket = socket.create_connection((host, int(port or 80)), timeout=timeout)
        self._socket.settimeout(timeout)
        self._buffer = b""
        key = base64.b64encode(secrets.token_bytes(16)).decode("ascii")
        handshake = (
            f"GET /{path} HTTP/1.1\r\n"
            f"Host: {hostport}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        )
        self._socket.sendall(handshake.encode("ascii"))
        header = self._read_until(b"\r\n\r\n")
        status = header.split(b"\r\n")[0]
        if b" 101 " not in status:
            raise CdpError(f"the upgrade was refused: {status!r}")

    def set_timeout(self, seconds: float) -> None:
        self._socket.settimeout(seconds)

    def _read_until(self, marker: bytes) -> bytes:
        while marker not in self._buffer:
            chunk = self._socket.recv(65536)
            if not chunk:
                raise CdpError("the connection closed during the handshake")
            self._buffer += chunk
        head, _, self._buffer = self._buffer.partition(marker)
        return head + marker

    def _read_exactly(self, count: int) -> bytes:
        while len(self._buffer) < count:
            chunk = self._socket.recv(max(65536, count - len(self._buffer)))
            if not chunk:
                raise CdpError("the connection closed mid-frame")
            self._buffer += chunk
        head, self._buffer = self._buffer[:count], self._buffer[count:]
        return head

    def send_text(self, text: str) -> None:
        payload = text.encode("utf-8")
        header = bytearray([0x80 | _TEXT])
        mask = secrets.token_bytes(4)
        length = len(payload)
        if length < 126:
            header.append(0x80 | length)
        elif length < (1 << 16):
            header.append(0x80 | 126)
            header += struct.pack(">H", length)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", length)
        header += mask
        masked = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))
        self._socket.sendall(bytes(header) + masked)

    def recv_text(self) -> str:
        """The next text frame, reassembled across continuations."""
        message = bytearray()
        while True:
            first, second = self._read_exactly(2)
            final = bool(first & 0x80)
            opcode = first & 0x0F
            length = second & 0x7F
            if length == 126:
                (length,) = struct.unpack(">H", self._read_exactly(2))
            elif length == 127:
                (length,) = struct.unpack(">Q", self._read_exactly(8))
            if second & 0x80:
                raise CdpError("a server frame was masked, which is a protocol error")
            payload = self._read_exactly(length)
            if opcode == _CLOSE:
                raise CdpError("the browser closed the connection")
            if opcode == _PING:
                self._send_control(_PONG, payload)
                continue
            if opcode == _PONG:
                continue
            if opcode in (_TEXT, _BINARY, 0x0):
                message += payload
                if final:
                    return message.decode("utf-8")
                continue
            raise CdpError(f"unexpected opcode {opcode}")

    def _send_control(self, opcode: int, payload: bytes) -> None:
        mask = secrets.token_bytes(4)
        masked = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))
        self._socket.sendall(bytes([0x80 | opcode, 0x80 | len(payload)]) + mask + masked)

    def close(self) -> None:
        try:
            self._send_control(_CLOSE, b"")
        except OSError:
            pass
        self._socket.close()


class Cdp:
    """Request/response correlation over one WebSocket, with event capture."""

    def __init__(self, url: str, timeout: float = 30.0) -> None:
        self._socket = WebSocket(url, timeout=timeout)
        self._timeout = timeout
        self._next_id = 1
        self.events: list[dict] = []

    def call(
        self,
        method: str,
        params: dict | None = None,
        session: str | None = None,
        timeout: float | None = None,
    ) -> dict:
        message_id = self._next_id
        self._next_id += 1
        message: dict = {"id": message_id, "method": method, "params": params or {}}
        if session:
            message["sessionId"] = session
        self._socket.set_timeout(timeout if timeout is not None else self._timeout)
        self._socket.send_text(json.dumps(message))
        try:
            while True:
                reply = json.loads(self._socket.recv_text())
                if reply.get("id") != message_id:
                    self.events.append(reply)
                    continue
                if "error" in reply:
                    raise CdpError(f"{method}: {reply['error']}")
                return reply.get("result", {})
        except (socket.timeout, TimeoutError) as failure:
            raise CdpTimeout(f"{method}: no answer from the target") from failure
        finally:
            self._socket.set_timeout(self._timeout)

    def drain(self, seconds: float) -> None:
        """Read whatever arrives for `seconds`, keeping events."""
        import time

        deadline = time.monotonic() + seconds
        try:
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return
                self._socket.set_timeout(remaining)
                try:
                    self.events.append(json.loads(self._socket.recv_text()))
                except (socket.timeout, TimeoutError):
                    return
        finally:
            self._socket.set_timeout(self._timeout)

    def close(self) -> None:
        self._socket.close()
