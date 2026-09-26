#!/usr/bin/env python3
"""Test that physics capture qualification rejects stale/contaminated PASS logs."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from verify_physics_capture import validate_capture  # noqa: E402


SHA = "a" * 64
PHYSICS_RESULT = (
    "[phys-test] result=PASS profile=walljump movement=1 air_control=1 air_steer_delta=26.1 "
    "air_release_drop=5.3 air_grounded=0 run_peak=64.0 brake_min=0.0 "
    "reverse_speed=64.0 jump_rise=24.0 dash_distance=37.9 dash_launch=1 "
    "dash_airborne=1 dash_launch_y=52.0 dash_launch_speed=140.0 target=1 approach=1 "
    "approach_ticks=49 start_gap=51.2 entry_gap=3.4 climb=1 climb_ticks=16 "
    "climb_entry_grounded=1 climb_first_step_dy=0.67 climb_first_step_snap=0.100 "
    "climb_first_step_ok=1 walljump=1 walljump_ok=1 walljump_xy=83.2 "
    "walljump_y=90.0 walljump_out=83.2 mantle=0 mantle_ok=0 clearance_samples=16 "
    "clearance_violations=0 model_gap=0.50 required_gap=0.50 end=(0,0,0) v_y=0"
)


def good_capture() -> str:
    return "\n".join([
        f"[artifact] rom=physics_autoplay.z64 sha256={SHA} size_bytes=1234",
        "[build] compiled=Sep 25 2026 08:48:33",
        "[mappack] v2 loaded rom:/lvl/forsyken-city/forsyken-city.mappack: 45 rooms",
        "[map_runtime] global collision loaded: 10618 tris, 20868 verts",
        "[mappack] booted rom:/lvl/forsyken-city/forsyken-city.mappack: 45 rooms",
        "[matcat] loaded 32 materials from rom:/lvl/forsyken-city/forsyken-city.manifest",
        "[texroom] loaded: 415 verts, 12 batches, 114 faces, 0 discarded block=yes",
        "[input] z=0 l=0 r=0 al=0 ar=0 | j=0 d=0 c=0 p=0",
        "[counters] near_batches=56 texture_uploads=9 vert_loads=56 syncs=56 pending=0 resident=13",
        "[phys-test] climb-target hit=1 height=9 max_height=9 face=5190 range=50.0",
        "[phys-test] climb-entry gap=3.35 ticks=49 pos=(1.7,36.9,41.8)",
        PHYSICS_RESULT,
        "",
    ])


def main() -> None:
    capture = good_capture()
    good = validate_capture(capture, SHA)
    assert not good, f"valid capture rejected: {good}"

    mantle_result = PHYSICS_RESULT.replace("profile=walljump", "profile=mantle")
    mantle_result = mantle_result.replace("walljump=1 walljump_ok=1", "walljump=0 walljump_ok=0")
    mantle_result = mantle_result.replace("mantle=0 mantle_ok=0", "mantle=1 mantle_ok=1")
    mantle_capture = good_capture().replace(PHYSICS_RESULT, mantle_result)
    assert not validate_capture(mantle_capture, SHA, "mantle"), "valid mantle capture rejected"
    missing_mantle = mantle_capture.replace("mantle=1 mantle_ok=1", "mantle=0 mantle_ok=0")
    assert any("physics gate mantle=0" in item for item in
               validate_capture(missing_mantle, SHA, "mantle")), \
        "mantle profile accepted without a mantle"
    corner_result = PHYSICS_RESULT.replace("profile=walljump", "profile=corner")
    corner_result = corner_result.replace("walljump=1 walljump_ok=1", "walljump=0 walljump_ok=0")
    corner_result = corner_result.replace(
        "clearance_samples=16", "corner_seen=1 corner_arrived=1 corner_events=1 "
        "corner_stable_ticks=10 corner_max_step=1.9 clearance_samples=24",
    )
    corner_capture = good_capture().replace(PHYSICS_RESULT, corner_result)
    assert not validate_capture(corner_capture, SHA, "corner"), \
        "valid real-map corner capture rejected"
    repeated_corner = corner_capture.replace("corner_events=1", "corner_events=2")
    assert any("corner_events=2" in item for item in
               validate_capture(repeated_corner, SHA, "corner")), \
        "corner route accepted repeated/unstable corner adoption"
    grounded_dash = good_capture().replace("dash_airborne=1", "dash_airborne=0")
    assert any("physics gate dash_airborne=0" in item for item in
               validate_capture(grounded_dash, SHA)), \
        "grounded dash accepted as an airborne dash launch"
    flat_dash = good_capture().replace("dash_launch_y=52.0", "dash_launch_y=0.0")
    assert any("dash_launch_y=0" in item for item in validate_capture(flat_dash, SHA)), \
        "flat dash accepted as an OG airborne launch"
    assert any("profile is 'walljump'" in item for item in
               validate_capture(capture, SHA, "mantle")), \
        "wall-jump capture accepted as mantle profile"
    prefixed = "\n".join(
        line if line.startswith("[artifact]") or not line else f"{i}:{line}"
        for i, line in enumerate(capture.splitlines(), 1)
    )
    assert not validate_capture(prefixed, SHA), "capture_baseline line numbers were not handled"

    contaminated = good_capture().replace(
        "[matcat] loaded 32 materials from rom:/lvl/forsyken-city/forsyken-city.manifest",
        "[matcat] malformed matmeta: rom:/lvl/forsyken-city/forsyken-city.matmeta",
    ).replace(
        "[texroom] loaded: 415 verts, 12 batches, 114 faces, 0 discarded block=yes",
        "[lvlroom] bad magic",
    ).replace(
        "[input] z=0 l=0 r=0 al=0 ar=0 | j=0 d=0 c=0 p=0",
        "[input] z=1 l=0 r=0 al=0 ar=0 | j=0 d=0 c=1 p=0",
    )
    rejected = validate_capture(contaminated, SHA)
    assert any("material catalog" in item for item in rejected), rejected
    assert any("room renderer" in item for item in rejected), rejected
    assert any("input is held" in item for item in rejected), rejected
    assert any("no textured room" in item for item in rejected), rejected

    mismatched = validate_capture(good_capture(), "b" * 64)
    assert any("does not match expected" in item for item in mismatched), mismatched

    capture_script = (ROOT / "tools" / "capture_baseline.sh").read_text(
        encoding="utf-8"
    )
    report_filters = [line for line in capture_script.splitlines() if "grep -nE" in line]
    assert any("input-trace" in line for line in report_filters), (
        "capture_baseline must preserve fixed-tick [input-trace] records"
    )

    print("PASS: verifier enforces wall-jump/mantle/corner profiles and rejects stale evidence")


if __name__ == "__main__":
    main()
