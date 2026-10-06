#!/usr/bin/env python3
import argparse
import http.server
import json
import os
import sys
import threading
import unittest
import urllib.parse

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import android_qualify
import x2ctl


class MockHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(url.query)
        if url.path == "/status":
            payload = {
                "frames_presented": 42,
                "guest_time_s": 1.5,
                "unbounded": True,
                "renderer_backend": "vulkan",
                "presentation_width": 1920,
                "presentation_height": 1080,
                "frame_ms_avg": 16.6,
                "frame_ms_min": 15.0,
                "frame_ms_max": 20.0,
                "frame_ms_p50": 16.0,
                "frame_ms_p95": 19.0,
                "frame_ms_p99": 20.0,
                "frame_sample_count": 41,
                "frame_intervals": 41,
                "pid": os.getpid(),
                "input_recording": {
                    "path": "scratch/test-live-input.jsonl",
                    "events": 2,
                },
                "control": {
                    "requests": 5,
                    "keys_pressed": 4,
                    "keys_refused": 0,
                    "screenshots": 1,
                },
            }
            body = json.dumps(payload).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif url.path == "/key":
            name = q.get("name", [""])[0]
            if name == "InvalidKey":
                self.send_response(409)
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                self.wfile.write(b"no DirectInput mapping for key")
            else:
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                self.wfile.write(('pressed "%s"' % name).encode("utf-8"))
        elif url.path == "/pad":
            btn = q.get("button", [""])[0]
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(('pad "%s"' % btn).encode("utf-8"))
        elif url.path == "/screenshot":
            dummy = b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15c4\x00\x00\x00\rIDATx\x9cc\xf8\xff\xff?\x00\x05\xfe\x02\xfe\xa748a\x00\x00\x00\x00IEND\xaeB`\x82"
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(dummy)))
            self.end_headers()
            self.wfile.write(dummy)
        elif url.path == "/input":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"bindings table")
        elif url.path == "/save":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(
                b"save-trace enabled=1 attempts=2 recorded=2 retained=2/64\n"
            )
        elif url.path == "/performance/reset":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"frame-time qualification window reset\n")
        else:
            self.send_response(404)
            self.end_headers()


class TestX2ctl(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.HTTPServer(("127.0.0.1", 0), MockHandler)
        cls.port = cls.server.server_port
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def test_live_session_selects_published_port(self):
        session = {
            "version": 1,
            "running": True,
            "pid": os.getpid(),
            "control_port": self.port,
        }
        self.assertEqual(x2ctl.resolve_port(None, session), self.port)
        self.assertEqual(x2ctl.resolve_port(9999, session), 9999)

    def test_cmd_status(self):
        rc = x2ctl.cmd_status(argparse.Namespace(port=self.port))
        self.assertEqual(rc, 0)

    def test_reset_frame_timing(self):
        android_qualify.reset_frame_timing(self.port)

    def test_cmd_key_ok(self):
        rc = x2ctl.cmd_key(argparse.Namespace(port=self.port, names=["Return"], hold=0.0, gap=0.0))
        self.assertEqual(rc, 0)

    def test_cmd_key_refused(self):
        rc = x2ctl.cmd_key(argparse.Namespace(port=self.port, names=["InvalidKey"], hold=0.0, gap=0.0))
        self.assertEqual(rc, 1)

    def test_cmd_pad(self):
        rc = x2ctl.cmd_pad(argparse.Namespace(port=self.port, names=["a", "leftx=0.5"], hold=0.0, gap=0.0))
        self.assertEqual(rc, 0)

    def test_cmd_shot(self):
        out = "scratch/test_out_shot.png"
        rc = x2ctl.cmd_shot(argparse.Namespace(port=self.port, out=out))
        self.assertEqual(rc, 0)
        self.assertTrue(os.path.exists(out))
        os.remove(out)

    def test_cmd_input(self):
        rc = x2ctl.cmd_input(argparse.Namespace(port=self.port, controller=0))
        self.assertEqual(rc, 0)

    def test_cmd_save(self):
        rc = x2ctl.cmd_save(argparse.Namespace(port=self.port))
        self.assertEqual(rc, 0)

    def test_cmd_probe_collects_bundle(self):
        recording = os.path.join("scratch", "test-live-input.jsonl")
        os.makedirs("scratch", exist_ok=True)
        with open(recording, "w", encoding="utf-8") as file:
            file.write('{"type":"session"}\n')
            file.write('{"type":"keyboard","down_dik":[17]}\n')
        session = {
            "version": 1,
            "running": True,
            "pid": os.getpid(),
            "control_port": self.port,
            "input_recording": recording,
        }
        args = argparse.Namespace(
            port=self.port,
            controller=0,
            events=5,
            shot="scratch/test-live-probe.png",
            live_session=session,
        )
        self.assertEqual(x2ctl.cmd_probe(args), 0)
        self.assertTrue(os.path.exists(args.shot))


if __name__ == "__main__":
    unittest.main()
