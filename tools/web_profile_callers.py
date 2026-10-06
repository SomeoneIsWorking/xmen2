#!/usr/bin/env python3
"""Who calls a function, or what runs beneath one, in a saved web profile.

WHY. Self time says where the samples landed and not why. A worker that spends
a tenth of its time in `emscripten_futex_wait` is waiting on something, and
what it waits on is only in the caller chain: a proxied call to the main
thread, a mutex, a fence. #165 asked the same question of the culling
subtree. This walks each sample of a named function up its stack and ranks
the chains that reached it; with --below it instead takes every sample whose
stack passes through the function -- its inclusive time -- and ranks where
those samples landed.

THE NEGATIVE IS DESIGNED. A name that matched no node, and a name whose nodes
collected no sample, are different answers: the first is usually a typo or an
unresolved frame, the second a function that simply did not run. Both are
refused with the count of what was scanned, never printed as an empty table.
"""

from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

from web_profile import _self_samples, load_symbols, resolve


def pick_profile(saved: list[dict], target: str | None) -> tuple[dict, str]:
    """The target named by an id prefix, else the one with the most samples."""
    if not saved:
        raise ValueError("the saved file holds no profiles")
    if target:
        chosen = [entry for entry in saved if entry["target"]["targetId"].startswith(target)]
        if len(chosen) != 1:
            raise ValueError(f"{len(chosen)} target(s) match {target!r} among {len(saved)}")
        entry = chosen[0]
    else:
        entry = max(saved, key=lambda item: sum(_self_samples(item["profile"]).values()))
    return entry["profile"], entry["target"]["targetId"][:8]


def caller_chains(
    profile: dict, symbols: dict[int, str], leaf: str, depth: int
) -> tuple[collections.Counter, int, int]:
    """(chains, matching nodes, samples in the profile) for every sample whose
    innermost frame resolves to `leaf`, each chain its callers innermost first."""
    nodes = {node["id"]: node for node in profile.get("nodes", [])}
    parent = {child: node["id"] for node in nodes.values() for child in node.get("children", [])}

    def name(node_id: int) -> str:
        frame = nodes[node_id]["callFrame"]
        return resolve(frame.get("functionName") or "(anonymous)", frame.get("url", ""), symbols)

    samples = _self_samples(profile)
    matching = {node_id for node_id in nodes if name(node_id) == leaf}
    chains: collections.Counter = collections.Counter()
    for node_id in matching:
        hits = samples.get(node_id, 0)
        if not hits:
            continue
        chain = []
        caller = parent.get(node_id)
        while caller is not None and len(chain) < depth:
            chain.append(name(caller))
            caller = parent.get(caller)
        chains[" < ".join(chain) or "(no caller)"] += hits
    return chains, len(matching), sum(samples.values())


def below(profile: dict, symbols: dict[int, str], frame: str) -> tuple[collections.Counter, int, int]:
    """(self samples by leaf, matching nodes, samples in the profile) for every
    sample with `frame` anywhere on its stack, the frame itself included."""
    nodes = {node["id"]: node for node in profile.get("nodes", [])}
    parent = {child: node["id"] for node in nodes.values() for child in node.get("children", [])}

    def name(node_id: int) -> str:
        node = nodes[node_id]["callFrame"]
        return resolve(node.get("functionName") or "(anonymous)", node.get("url", ""), symbols)

    samples = _self_samples(profile)
    matching = {node_id for node_id in nodes if name(node_id) == frame}
    leaves: collections.Counter = collections.Counter()
    for node_id, hits in samples.items():
        ancestor = node_id
        while ancestor is not None and ancestor not in matching:
            ancestor = parent.get(ancestor)
        if ancestor is not None:
            leaves[name(node_id)] += hits
    return leaves, len(matching), sum(samples.values())


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("profile", type=Path, help="a file web_profile.py --save wrote")
    parser.add_argument("leaf", help="the resolved function name to explain")
    parser.add_argument("--below", action="store_true", help="rank what runs beneath it instead")
    parser.add_argument("--target", default=None, help="target id prefix; default the busiest")
    parser.add_argument("--depth", type=int, default=10)
    parser.add_argument("--top", type=int, default=12)
    parser.add_argument("--symbols", default="build/web/x2native.js.symbols")
    args = parser.parse_args(argv)

    with args.profile.open(encoding="utf-8") as handle:
        profile, label = pick_profile(json.load(handle), args.target)
    symbols = load_symbols(args.symbols)
    if args.below:
        chains, matched, total = below(profile, symbols, args.leaf)
    else:
        chains, matched, total = caller_chains(profile, symbols, args.leaf, args.depth)
    reached = sum(chains.values())
    if not matched:
        print(f"target {label}: no node of {len(profile.get('nodes', []))} resolves to {args.leaf!r}")
        return 1
    if not reached:
        print(f"target {label}: {matched} node(s) named {args.leaf!r} collected 0 of {total} sample(s)")
        return 1
    where = "beneath" if args.below else "in"
    print(f"target {label}: {reached} of {total} sample(s) {where} {args.leaf} ({100.0 * reached / total:.2f}%)")
    for chain, hits in chains.most_common(args.top):
        print(f"  {100.0 * hits / reached:6.2f}%  {hits:7d}  {chain}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
