#!/usr/bin/env python3
"""Summarize pre-build draws bound to a selector-sized texture."""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

FORMAT_VERSION = 5
TARGET = "texture-dimensions"
FINGERPRINT_ALGORITHM = "fnv1a64-v1"
FINGERPRINT_SCOPE = "2d-level0-after-successful-upload"
STATE_FIELDS = (
    "zenable", "zwrite", "zfunc", "zbias", "blend_enable", "src_blend",
    "dst_blend", "alpha_test", "alpha_ref", "alpha_func", "cull",
    "color_op", "color_arg1", "color_arg2", "alpha_op", "alpha_arg1",
    "alpha_arg2", "texcoord_index", "texture_transform", "address_u",
    "address_v", "mag_filter", "min_filter", "mip_filter",
)


class Refuse(RuntimeError):
    """The input cannot support the claimed conclusion."""


@dataclass(frozen=True)
class Summary:
    candidates: tuple[dict[str, Any], ...]
    results: tuple[dict[str, Any], ...]
    target_width: int
    target_height: int

    @property
    def accepted(self) -> tuple[dict[str, Any], ...]:
        return tuple(result for result in self.results if result["accepted"])

    @property
    def refused(self) -> tuple[dict[str, Any], ...]:
        return tuple(result for result in self.results if not result["accepted"])


def parse_records(path: Path) -> list[dict[str, Any]]:
    records = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise Refuse(f"selector_probe: cannot read {path}: {exc}") from exc
    for line_number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as exc:
            raise Refuse(
                f"selector_probe: {path}:{line_number}: invalid JSON: {exc}"
            ) from exc
        if not isinstance(value, dict) or not isinstance(value.get("event"), str):
            raise Refuse(
                f"selector_probe: {path}:{line_number}: record has no event"
            )
        records.append(value)
    return records


def _record_key(record: dict[str, Any]) -> tuple[int, int]:
    frame, order = record.get("frame"), record.get("order")
    if type(frame) is not int or type(order) is not int or frame < 0 or order < 1:
        raise Refuse("selector_probe: candidate/result has invalid frame or order")
    return frame, order


def _is_hex(value: Any, digits: int) -> bool:
    return (
        isinstance(value, str)
        and len(value) == digits
        and all(char in "0123456789abcdef" for char in value)
    )


def _validate_bounds(candidate: dict[str, Any], key: tuple[int, int]) -> None:
    counts = (
        "elements_requested", "elements_used", "behind", "out_of_range",
        "unavailable",
    )
    if any(type(candidate.get(field)) is not int or candidate[field] < 0
           for field in counts):
        raise Refuse(f"selector_probe: candidate at {key} has invalid counts")
    accounted = (
        candidate["elements_used"]
        + candidate["behind"]
        + candidate["out_of_range"]
        + candidate["unavailable"]
    )
    if accounted != candidate["elements_requested"]:
        raise Refuse(f"selector_probe: candidate at {key} loses geometry elements")
    if type(candidate.get("bounds_valid")) is not bool:
        raise Refuse(f"selector_probe: candidate at {key} has no bounds validity")
    if candidate["bounds_valid"] != (candidate["elements_used"] != 0):
        raise Refuse(f"selector_probe: candidate at {key} contradicts its bounds")
    if not candidate["bounds_valid"]:
        return
    names = ("min_x", "min_y", "min_z", "max_x", "max_y", "max_z")
    if any(type(candidate.get(name)) not in (int, float)
           or not math.isfinite(candidate[name]) for name in names):
        raise Refuse(f"selector_probe: candidate at {key} has invalid bounds")
    if any(candidate[low] > candidate[high] for low, high in (
        ("min_x", "max_x"), ("min_y", "max_y"), ("min_z", "max_z")
    )):
        raise Refuse(f"selector_probe: candidate at {key} has inverted bounds")


def _validate_candidate(
    candidate: dict[str, Any],
    key: tuple[int, int],
    target_width: int,
    target_height: int,
) -> None:
    width = candidate.get("texture_width")
    height = candidate.get("texture_height")
    if type(width) is not int or type(height) is not int \
            or width != target_width or height != target_height:
        raise Refuse(f"selector_probe: candidate at {key} misses target dimensions")
    if not _is_hex(candidate.get("texture_guest"), 8):
        raise Refuse(f"selector_probe: candidate at {key} has invalid texture binding")
    bool_fields = (
        "texture_resolved", "fingerprint_available", "indexed", "layout_valid",
        "pretransformed", "positions_truncated",
    )
    if any(type(candidate.get(field)) is not bool for field in bool_fields):
        raise Refuse(f"selector_probe: candidate at {key} has invalid flags")
    fingerprint = candidate.get("fingerprint")
    if candidate["fingerprint_available"]:
        if not _is_hex(fingerprint, 16) \
                or type(candidate.get("fingerprint_revision")) is not int \
                or candidate["fingerprint_revision"] < 1:
            raise Refuse(f"selector_probe: candidate at {key} has invalid fingerprint")
    elif fingerprint is not None or candidate.get("fingerprint_revision") != 0:
        raise Refuse(f"selector_probe: candidate at {key} invents a fingerprint")
    integer_fields = (
        "texture_format", "texture_levels", "texture_faces", "primitive",
        "primitive_count", "elements", "vertex_stride", "position_offset",
        "vertex_bytes", "index_bytes", *STATE_FIELDS,
    )
    if any(type(candidate.get(field)) is not int for field in integer_fields):
        raise Refuse(f"selector_probe: candidate at {key} has incomplete state")
    if candidate["elements"] != candidate["elements_requested"]:
        raise Refuse(f"selector_probe: candidate at {key} changes its denominator")
    if not _is_hex(candidate.get("fvf"), 8):
        raise Refuse(f"selector_probe: candidate at {key} has invalid FVF")
    samples = candidate.get("position_samples")
    expected_samples = min(candidate["elements"], 12)
    if not isinstance(samples, list) or len(samples) != expected_samples \
            or candidate["positions_truncated"] != (candidate["elements"] > 12):
        raise Refuse(f"selector_probe: candidate at {key} has invalid samples")
    for sample in samples:
        if sample is None:
            continue
        if not isinstance(sample, list) or len(sample) not in (3, 4) \
                or not all(_is_hex(word, 8) for word in sample):
            raise Refuse(f"selector_probe: candidate at {key} has invalid samples")
    _validate_bounds(candidate, key)


def summarize(records: list[dict[str, Any]]) -> Summary:
    meta = [record for record in records if record.get("event") == "meta"]
    expected = {
        "version": FORMAT_VERSION,
        "target": TARGET,
        "identity_claim": False,
        "fingerprint_algorithm": FINGERPRINT_ALGORITHM,
        "fingerprint_scope": FINGERPRINT_SCOPE,
    }
    if len(meta) != 1:
        raise Refuse("selector_probe: evidence has no unique meta record")
    mismatches = [
        f"{key}={meta[0].get(key)!r}"
        for key, value in expected.items()
        if meta[0].get(key) != value
    ]
    if mismatches:
        raise Refuse("selector_probe: incompatible evidence: " + ", ".join(mismatches))
    target_width = meta[0].get("texture_width")
    target_height = meta[0].get("texture_height")
    if type(target_width) is not int or type(target_height) is not int \
            or target_width < 1 or target_height < 1:
        raise Refuse("selector_probe: meta has invalid target dimensions")
    candidates = tuple(
        record for record in records if record.get("event") == "candidate"
    )
    if not candidates:
        raise Refuse(
            f"selector_probe: no {target_width}x{target_height} texture-bound "
            "build request was observed; "
            "the target path was not reached by this instrument"
        )
    candidate_by_key = {}
    for candidate in candidates:
        key = _record_key(candidate)
        if key in candidate_by_key:
            raise Refuse(f"selector_probe: duplicate candidate at {key}")
        _validate_candidate(candidate, key, target_width, target_height)
        candidate_by_key[key] = candidate
    result_by_key = {}
    for result in (record for record in records if record.get("event") == "result"):
        key = _record_key(result)
        if key in result_by_key:
            raise Refuse(f"selector_probe: duplicate result at {key}")
        if key not in candidate_by_key or type(result.get("accepted")) is not bool:
            raise Refuse(f"selector_probe: orphan or invalid result at {key}")
        result_by_key[key] = result
    missing = [key for key in candidate_by_key if key not in result_by_key]
    if missing:
        raise Refuse(f"selector_probe: candidate {missing[0]} has no build result")
    results = tuple(result_by_key[_record_key(candidate)] for candidate in candidates)
    return Summary(candidates, results, target_width, target_height)


def report(path: Path, show_all: bool = False) -> None:
    summary = summarize(parse_records(path))
    print(
        "selector_probe: dimension match only; no asset or scene identity is claimed"
    )
    print(
        f"selector_probe: target {summary.target_width}x{summary.target_height}; "
        f"{len(summary.candidates)} build request(s), "
        f"{len(summary.accepted)} build-accepted, {len(summary.refused)} build-refused"
    )
    fingerprints = {
        candidate["fingerprint"]
        for candidate in summary.candidates
        if candidate["fingerprint_available"]
    }
    unavailable = sum(
        not candidate["fingerprint_available"] for candidate in summary.candidates
    )
    print(
        f"selector_probe: {len(fingerprints)} level-0 fingerprint(s); "
        f"{unavailable} request(s) had no committed level-0 fingerprint"
    )
    bounded = [candidate for candidate in summary.candidates
               if candidate["bounds_valid"]]
    if bounded:
        print(
            "selector_probe: transformed envelope "
            f"x={min(item['min_x'] for item in bounded)}.."
            f"{max(item['max_x'] for item in bounded)} "
            f"y={min(item['min_y'] for item in bounded)}.."
            f"{max(item['max_y'] for item in bounded)} "
            f"z={min(item['min_z'] for item in bounded)}.."
            f"{max(item['max_z'] for item in bounded)}; "
            f"{len(bounded)}/{len(summary.candidates)} valid"
        )
    selected = summary.candidates if show_all else (
        summary.candidates[0], summary.candidates[-1]
    )
    if not show_all and len(summary.candidates) == 1:
        selected = summary.candidates
    result_by_key = {_record_key(result): result for result in summary.results}
    for candidate in selected:
        key = _record_key(candidate)
        bounds = (
            f"x={candidate['min_x']}..{candidate['max_x']} "
            f"y={candidate['min_y']}..{candidate['max_y']} "
            f"z={candidate['min_z']}..{candidate['max_z']}"
            if candidate["bounds_valid"] else "bounds=unavailable"
        )
        print(
            f"  frame {key[0]} draw {key[1]}: build "
            f"{'accepted' if result_by_key[key]['accepted'] else 'refused'}; "
            f"texture {candidate['texture_guest']} "
            f"fingerprint={candidate['fingerprint']}; {bounds}; "
            f"geometry={candidate['elements_used']}/"
            f"{candidate['elements_requested']} used "
            f"({candidate['behind']} behind, "
            f"{candidate['out_of_range']} out-of-range, "
            f"{candidate['unavailable']} unavailable); "
            f"primitive {candidate['primitive']}/{candidate['primitive_count']} "
            f"stride={candidate['vertex_stride']} fvf={candidate['fvf']}"
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jsonl", type=Path)
    parser.add_argument("--all", action="store_true", help="print every request")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        report(args.jsonl, args.all)
    except Refuse as exc:
        print(exc, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
