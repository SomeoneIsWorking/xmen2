#!/usr/bin/env python3
"""Regression checks for the frontier's canonical Markdown writer."""

import importlib.util
from pathlib import Path
import tempfile


ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("re_frontier", ROOT / "tools" / "re_frontier.py")
frontier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frontier)

raw = ROOT / "scratch" / "raw"
raw.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="frontier-test-", dir=raw) as temporary:
    frontier.ROADMAP = str(Path(temporary) / "roadmap.md")
    one = frontier.Entry("one", "First", "area")
    one.status = "re-partial"
    one.evidence = "measured"
    two = frontier.Entry("two", "Second", "area")
    two.deps = ["one"]
    frontier.save({"one": one, "two": two}, ["one", "two"])
    text = Path(frontier.ROADMAP).read_text()

bad = [line for line in text.splitlines() if line.endswith(" ")]
assert not bad, f"serialized trailing whitespace: {bad!r}"
assert text.endswith("\n") and not text.endswith("\n\n"), "needs exactly one final newline"
assert "- deps:\n" in text, "empty dependencies were not serialized"
assert "- deps: one\n" in text, "non-empty dependencies were not serialized"
print("test_re_frontier: empty/non-empty fields have canonical whitespace")
