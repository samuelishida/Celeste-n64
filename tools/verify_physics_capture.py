#!/usr/bin/env python3
"""Reject physics-autoplay captures that lack artifact/runtime/input proof."""

from __future__ import annotations

import argparse
import math
import re
import sys
from pathlib import Path


SHA256 = re.compile(r"^[0-9a-fA-F]{64}$")
KEY_VALUE = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)")


def _normalize_capture(text: str) -> str:
    # capture_baseline prefixes selected serial lines with their raw-log line
    # number; manually pasted Ares logs do not have that prefix.
    return "\n".join(re.sub(r"^\d+:(?=\[)", "", line) for line in text.splitlines())


def _tag_lines(text: str, tag: str) -> list[str]:
    return re.findall(rf"(?m)^\[{re.escape(tag)}\].*$", text)


def _fields(line: str) -> dict[str, str]:
    return dict(KEY_VALUE.findall(line))


def _number(fields: dict[str, str], name: str, issues: list[str]) -> float | None:
    value = fields.get(name)
    if value is None:
        issues.append(f"physics result missing {name}")
        return None
    try:
        number = float(value)
    except ValueError:
        issues.append(f"physics result has invalid {name}={value!r}")
        return None
    if not math.isfinite(number):
        issues.append(f"physics result has non-finite {name}")
        return None
    return number


def validate_capture(
    text: str, expected_sha256: str, profile: str = "walljump"
) -> list[str]:
    """Return all reasons capture cannot be accepted as a physics PASS."""
    issues: list[str] = []
    if not SHA256.fullmatch(expected_sha256):
        return ["expected ROM SHA-256 must be 64 hexadecimal characters"]
    if profile not in {"walljump", "mantle", "corner"}:
        return ["profile must be 'walljump', 'mantle', or 'corner'"]

    text = _normalize_capture(text)
    artifact_lines = _tag_lines(text, "artifact")
    if len(artifact_lines) != 1:
        issues.append(f"expected one [artifact] line, got {len(artifact_lines)}")
    else:
        artifact_hash = re.search(r"\bsha256=([0-9a-fA-F]{64})\b", artifact_lines[0])
        if not artifact_hash:
            issues.append("[artifact] line has no valid sha256")
        elif artifact_hash.group(1).lower() != expected_sha256.lower():
            issues.append("capture ROM SHA-256 does not match expected artifact")

    if not re.search(r"(?m)^\[build\] compiled=\S", text):
        issues.append("missing ROM [build] marker")
    if not re.search(r"(?m)^\[mappack\] v2 loaded .*:\s*[1-9]\d* rooms\b", text):
        issues.append("map-pack v2 did not load any rooms")
    if not re.search(r"(?m)^\[map_runtime\] global collision loaded:\s*[1-9]\d* tris\b", text):
        issues.append("global map collision did not load")
    if not re.search(r"(?m)^\[mappack\] booted .*:\s*[1-9]\d* rooms\b", text):
        issues.append("map-pack did not boot")

    material_lines = _tag_lines(text, "matcat")
    if not any(re.search(r"\bloaded\s+[1-9]\d* materials\b", line)
               for line in material_lines):
        issues.append("material catalog did not load")
    room_lines = _tag_lines(text, "texroom")
    room_loaded = False
    for line in room_lines:
        match = re.search(
            r"loaded:\s*\d+ verts,\s*(\d+) batches,\s*(\d+) faces,\s*(\d+) discarded",
            line,
        )
        if match and int(match.group(1)) > 0 and int(match.group(2)) > 0 and int(match.group(3)) == 0:
            room_loaded = True
            break
    if not room_loaded:
        issues.append("no textured room loaded with geometry and zero discarded faces")

    failure_patterns = (
        (r"^\[(?:mappack|map_runtime)\].*\b(?:FAILED|failed)\b", "map boot/runtime failure"),
        (r"^\[matcat\].*\b(?:malformed|FAILED|MISSING|mismatch)\b", "material catalog error"),
        (r"^\[(?:texroom|lvlroom)\].*\b(?:FAILED|bad magic|bad version|empty level)\b", "room renderer error"),
    )
    for pattern, description in failure_patterns:
        if re.search(rf"(?m){pattern}", text):
            issues.append(description)

    input_lines = _tag_lines(text, "input")
    if not input_lines:
        issues.append("capture has no raw controller input samples")
    for line_no, line in enumerate(input_lines, 1):
        match = re.search(r"\bz=(\d+)\s+l=(\d+)\s+r=(\d+)\s+al=(\d+)\s+ar=(\d+)\b", line)
        if not match:
            issues.append(f"raw input sample {line_no} is malformed")
            continue
        if any(int(bit) != 0 for bit in match.groups()):
            issues.append(f"raw shoulder/analog input is held in sample {line_no}")
            break

    counter_lines = _tag_lines(text, "counters")
    if not any(
        (near := re.search(r"\bnear_batches=(\d+)\b", line))
        and int(near.group(1)) > 0
        for line in counter_lines
    ):
        issues.append("render counters show no near geometry")

    # The runtime emits diagnostic events (climb-target, climb-entry) with the
    # same tag before its terminal result. Count only terminal result records.
    result_lines = [
        line for line in _tag_lines(text, "phys-test")
        if re.match(r"^\[phys-test\]\s+result=", line)
    ]
    if len(result_lines) != 1:
        issues.append(f"expected one [phys-test] result, got {len(result_lines)}")
        return issues
    result = _fields(result_lines[0])
    if result.get("result") != "PASS":
        issues.append(f"physics autoplay result is {result.get('result', 'missing')}")
    if result.get("profile") != profile:
        issues.append(
            f"physics capture profile is {result.get('profile', 'missing')!r}, expected {profile!r}"
        )

    required_true = [
        "movement", "air_control", "dash_launch", "dash_airborne", "target", "approach", "climb",
        "climb_entry_grounded", "climb_first_step_ok",
    ]
    if profile == "walljump":
        required_true.extend(("walljump", "walljump_ok"))
    elif profile == "mantle":
        required_true.extend(("mantle", "mantle_ok"))
    else:
        required_true.extend(("corner_seen", "corner_arrived"))
    for name in required_true:
        value = _number(result, name, issues)
        if value is not None and value != 1:
            issues.append(f"physics gate {name}={value:g}, expected 1")

    threshold_checks = (
        ("air_steer_delta", 8.0, "min"),
        ("air_release_drop", 4.0, "min"),
        ("air_grounded", 0.0, "eq"),
        ("run_peak", 60.0, "min"),
        ("brake_min", 1.0, "max"),
        ("reverse_speed", 50.0, "min"),
        ("jump_rise", 15.0, "min"),
        ("dash_distance", 20.0, "min"),
        ("dash_launch_y", 50.0, "min"),
        ("dash_launch_y", 54.0, "max"),
        ("dash_launch_speed", 138.0, "min"),
        ("dash_launch_speed", 142.0, "max"),
        ("approach_ticks", 1.0, "min"),
        ("entry_gap", 4.01, "max"),
        ("climb_ticks", 10.0 if profile == "corner" else 4.0, "min"),
        ("climb_first_step_dy", 0.5, "min"),
        ("climb_first_step_snap", 0.0, "gt"),
        ("clearance_samples", 10.0, "min"),
        ("clearance_violations", 0.0, "eq"),
    )
    if profile != "corner":
        threshold_checks += (("climb_rise", 4.0, "min"),)
    if profile == "walljump":
        threshold_checks += (
            ("walljump_xy", 82.2, "min"),
            ("walljump_y", 89.0, "min"),
            ("walljump_out", 82.2, "min"),
        )
    elif profile == "corner":
        threshold_checks += (
            ("corner_events", 1.0, "eq"),
            ("corner_stable_ticks", 10.0, "min"),
            ("corner_max_step", 6.0, "max"),
        )
    values: dict[str, float] = {}
    for name, bound, operation in threshold_checks:
        value = _number(result, name, issues)
        if value is None:
            continue
        values[name] = value
        failed = {
            "min": value < bound,
            "max": value > bound,
            "eq": value != bound,
            "gt": value <= bound,
        }[operation]
        if failed:
            relation = {"min": ">=", "max": "<=", "eq": "==", "gt": ">"}[operation]
            issues.append(f"physics metric {name}={value:g}, expected {relation} {bound:g}")

    model_gap = _number(result, "model_gap", issues)
    required_gap = _number(result, "required_gap", issues)
    if model_gap is not None and required_gap is not None and model_gap < required_gap - 0.01:
        issues.append("rendered player model clearance is below required gap")
    start_gap = _number(result, "start_gap", issues)
    if start_gap is not None and "entry_gap" in values and start_gap - values["entry_gap"] < 4.0:
        issues.append("guided climb approach did not cover at least 4 world units")

    # Animation-alignment telemetry: parse [anim-align] lines and compute
    # max angular delta between visual facing and target facing while climbing.
    # Only the corner profile requires the metric to be present for now.
    anim_align_lines = _tag_lines(text, "anim-align")
    climb_max_delta = 0.0
    corner_max_delta = 0.0
    corner_triggered_ticks: set[int] = set()
    for line in _tag_lines(text, "climb"):
        fields = _fields(line)
        if fields.get("event") == "corner_triggered":
            tick_str = fields.get("tick")
            if tick_str is not None and tick_str.isdigit():
                corner_triggered_ticks.add(int(tick_str))
    previous_target = None
    stable_target_samples = 0
    unsettled_facing = False
    for line in anim_align_lines:
        fields = _fields(line)
        facing = _number(fields, "facing", issues)
        target = _number(fields, "target", issues)
        if facing is None or target is None:
            continue
        delta = abs((target - facing + math.pi) % (2.0 * math.pi) - math.pi)
        delta_deg = math.degrees(delta)
        target_change = abs((target - previous_target + math.pi) % (2.0 * math.pi) - math.pi) if previous_target is not None else math.inf
        stable_target_samples = stable_target_samples + 1 if target_change < 0.001 else 1
        previous_target = target
        # Source Facing approaches at4pi/s: transient lag after a target change
        # is expected. Reject persistent lag after eight stable observed frames.
        unsettled_facing |= stable_target_samples >= 8 and delta_deg >= 2.0
        if delta_deg > climb_max_delta:
            climb_max_delta = delta_deg
        tick_str = fields.get("tick")
        if tick_str is not None and tick_str.isdigit() and int(tick_str) in corner_triggered_ticks:
            if delta_deg > corner_max_delta:
                corner_max_delta = delta_deg
    if anim_align_lines:
        values["climb_max_facing_delta_deg"] = climb_max_delta
        values["corner_max_facing_delta_deg"] = corner_max_delta
    if anim_align_lines and profile == "corner" and unsettled_facing:
        issues.append("climb facing did not converge after eight stable target samples")

    return issues


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="capture_baseline summary file")
    parser.add_argument("--expected-sha256", required=True, help="SHA-256 of ROM captured")
    parser.add_argument(
        "--profile", choices=("walljump", "mantle", "corner"), default="walljump"
    )
    args = parser.parse_args(argv)
    try:
        text = args.capture.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"FAIL: cannot read capture {args.capture}: {exc}", file=sys.stderr)
        return 2

    issues = validate_capture(text, args.expected_sha256, args.profile)
    if issues:
        for issue in issues:
            print(f"FAIL: {issue}", file=sys.stderr)
        return 1
    print(f"PASS: physics capture qualified for ROM SHA-256 {args.expected_sha256.lower()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
