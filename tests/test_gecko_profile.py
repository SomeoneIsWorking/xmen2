"""The Gecko attribution's reading of a profile of the game tab's content process.

Paired cases: a profile whose pthread workers are the busiest threads is
reported with a denominator and resolved names, and every profile that could
have produced a confident but wrong answer is refused -- no threads, no working
thread, a warm-up that swallowed every sample, a symbol map that named nothing.
"""

import io
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from tempfile import TemporaryDirectory

from tools.gecko_profile import ProfileError, report, summarize_file, thread_rows
from tools.web_profile import CdpError

PORT_URL = "http://127.0.0.1:8617/x2native.wasm"


def func(row: int, name: str, url: str) -> dict:
    return {"name": [name], "fileName": [url]}


def thread(name: str, tid: int, process_type: str, frames: list[tuple[str, str]],
           stacks: list[list[int]], times_ms: list[float]) -> dict:
    """One Gecko thread: `frames` are (name, url) leaf-first, one per stack slot.

    func 0 is the thread itself, so a frame's name and file string sit at
    2i+1 and 2i+2, exactly as a Gecko string table would hold them.
    """
    strings = [name] + [part for fn, url in frames for part in (fn, url)]
    # func i is frame i's function, whose name and file strings sit at 2i+1 and
    # 2i+2; frame and stack indices are both 0-based, as Gecko's are.
    indices = list(range(len(frames)))
    return {
        "name": name,
        "tid": tid,
        "processType": process_type,
        "stringTable": strings,
        "funcTable": {"name": [2 * index + 1 for index in indices],
                      "fileName": [2 * index + 2 for index in indices]},
        "frameTable": {"func": indices},
        "stackTable": {"frame": indices},
        "samples": {"time": [int(ms * 1000) for ms in times_ms], "data": stacks},
    }


def profile(threads: list[dict], window_ms: float = 1000.0) -> dict:
    return {"meta": {"startTime": 0, "endTime": int(window_ms * 1000),
                     "version": 30, "processType": "tab"},
            "threads": threads}


def guest_worker(tid: int = 7, samples: int = 20, start_ms: float = 0.0) -> dict:
    """A pthread worker: half the samples sit inside the JIT, half in dispatch."""
    return thread(
        "GeckoWorker pthread", tid, "tab",
        [("x86p_jit_engine_run", PORT_URL), ("x86_engine_jit_run", PORT_URL),
         ("(idle)", "resource://gre/modules/libFavicon.jsm")],
        [[0, 1], [0]] * samples,
        [start_ms + 10.0 * i for i in range(2 * samples)])


def symbols_file(directory: Path, count: int = 64) -> Path:
    path = directory / "x2native.js.symbols"
    path.write_text("".join(f"{index}:x2p_sym_{index}\n" for index in range(count)))
    return path


def quiet_report(profile_doc: dict, symbols: dict[int, str], top: int = 30,
                 since: float = 0.0) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        try:
            code = report(profile_doc, symbols, Path("x2native.js.symbols"), top, since)
        except (ProfileError, CdpError) as error:
            code, out, err = 1, io.StringIO(), err
            print(f"refused: {error}", file=err)
    return code, out.getvalue(), err.getvalue()


class GeckoProfileTest(unittest.TestCase):
    def test_reports_the_busiest_guest_worker_with_a_denominator(self) -> None:
        document = profile([guest_worker(), thread(
            "GeckoMain", 1, "tab", [("(idle)", "resource://gre/modules/libFavicon.jsm")],
            [[0]] * 500, [10.0] * 500)])
        code, out, _ = quiet_report(document, {0: "x86p_jit_engine_run"})
        self.assertEqual(code, 0)
        self.assertIn("2 thread(s) sampled", out)
        self.assertIn("scanned 540 sample(s)", out)
        self.assertIn("BUSIEST GUEST WORKER GeckoWorker pthread tid 7 (tab)", out)
        # GeckoMain collected 500 samples to the worker's 40, and the report
        # still names the worker: sample COUNT is not what identifies the
        # thread doing the work, so the ranking has to be by working samples.
        share = out.split("sample share by thread")[1]
        self.assertLess(share.index("GeckoWorker pthread"),
                        share.index("GeckoMain"))

    def test_self_and_total_rankings_differ_for_a_caller(self) -> None:
        document = profile([guest_worker()])
        _, out, _ = quiet_report(document, {0: "x86p_jit_engine_run"})
        by_self, by_total = out.split("top 30 by self time")[1].split("top 30 by total time")
        # Half the samples were taken inside the JIT, and the dispatch frame is
        # on the stack of every one of them: self counts one, total counts all.
        self.assertIn("x86p_jit_engine_run", by_self)
        self.assertNotIn("x86_engine_jit_run", by_self)
        self.assertIn("x86_engine_jit_run", by_total)

    def test_resolves_wasm_indices_only_in_the_port_module(self) -> None:
        named = {12: "x86p_jit_engine_run"}
        document = profile([thread(
            "worker", 3, "tab",
            [("wasm-function[12]", PORT_URL), ("wasm-function[12]", "http://x/anon.wasm")],
            [[0], [1]] * 10, [10.0] * 20)])
        rows = thread_rows(document, named, 0.0, 0.0)
        labels = set(rows[0].self_by_func)
        self.assertIn("x86p_jit_engine_run  [x2native.wasm]", labels)
        # Index 12 of a translated guest block's own module may not borrow the
        # port's name; that is the resolver's existing contract, exercised here.
        self.assertIn("wasm-function[12]  [anon.wasm]", labels)

    def test_drops_the_warm_up_but_reports_how_many_survived(self) -> None:
        document = profile([guest_worker(samples=20, start_ms=90_000.0)])
        code, out, err = quiet_report(document, {0: "x86p_jit_engine_run"}, since=60.0)
        self.assertEqual(code, 0)
        self.assertIn("40 sample(s)", out)
        self.assertIn("excluded from every share", out)
        self.assertEqual(err, "")

    def test_refuses_a_profile_with_no_working_thread(self) -> None:
        document = profile([guest_worker()])
        code, _, err = quiet_report(document, {}, since=1000.0)
        self.assertEqual(code, 1)
        self.assertIn("no working samples", err)

    def test_refuses_a_file_without_threads(self) -> None:
        code, _, err = quiet_report({"meta": {}}, {0: "x"})
        self.assertEqual(code, 1)
        self.assertIn("not a Gecko profile", err)

    def test_refuses_a_symbol_map_that_named_nothing(self) -> None:
        with TemporaryDirectory() as raw:
            path = Path(raw) / "gecko.json"
            path.write_text(__import__("json").dumps(profile([guest_worker()])))
            empty = Path(raw) / "empty.symbols"
            empty.write_text("not a symbol map\n")
            err = io.StringIO()
            with redirect_stderr(err), self.assertRaises(CdpError):
                summarize_file(path, empty, 30, 0.0)


if __name__ == "__main__":
    unittest.main()
