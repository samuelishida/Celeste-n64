#!/usr/bin/env python3
"""Generate deterministic source-derived Celeste 64 parity fixtures.

These fixtures intentionally model source mechanics from checked-in C# constants
without running the C# game. Later increments can compare the N64 runtime against
the same scenario vocabulary while a real capture path is still unavailable.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Callable


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TRACE_DIR = ROOT / "tests" / "movement_traces"
SOURCE_FILE = "Celeste64-og/Source/Actors/Player.cs"
DT = 1.0 / 60.0

# Player.cs constants, kept in source units (+Z is up in the C# reference).
C = {
    "acceleration": 500.0,
    "friction": 800.0,
    "gravity": 600.0,
    "jump_hold_time": 0.1,
    "jump_speed": 90.0,
    "jump_xy_boost": 10.0,
    "max_speed": 64.0,
    "dash_speed": 140.0,
    "dash_time": 0.2,
    "dash_end_speed_mult": 0.75,
    "skidding_start_accel": 300.0,
    "skidding_accel": 500.0,
    "wall_jump_xy_speed": 64.0 * 1.3,
    "climb_speed": 40.0,
    "platform_velocity_storage": 0.1,
}


def q(value: float) -> float:
    return round(value + 0.0, 6)


def frame(index: int, pos: tuple[float, float, float], vel: tuple[float, float, float], state: str, **extra: object) -> dict[str, object]:
    row: dict[str, object] = {
        "frame": index,
        "position": [q(v) for v in pos],
        "velocity": [q(v) for v in vel],
        "state": state,
    }
    row.update(extra)
    return row


def jump_full_hold() -> list[dict[str, object]]:
    z = 0.0
    vz = C["jump_speed"]
    rows = []
    for i in range(10):
        holding = i * DT < C["jump_hold_time"]
        if not holding:
            vz -= C["gravity"] * DT
        z += vz * DT
        rows.append(frame(i, (0.0, 0.0, z), (0.0, 0.0, vz), "Normal", grounded=False, jump_held=holding))
    return rows


def dash_air() -> list[dict[str, object]]:
    xy = C["dash_speed"] / math.sqrt(1.0 + 0.4 * 0.4)
    z_speed = xy * 0.4
    rows = []
    x = z = 0.0
    for i in range(15):
        dashing = i * DT < C["dash_time"]
        speed_mult = 1.0 if dashing else C["dash_end_speed_mult"]
        vx = xy * speed_mult
        vz = z_speed * speed_mult
        x += vx * DT
        z += vz * DT
        rows.append(frame(i, (x, 0.0, z), (vx, 0.0, vz), "Dashing" if dashing else "Normal", grounded=False))
    return rows


def skid_reverse() -> list[dict[str, object]]:
    rows = []
    x = 0.0
    vx = C["max_speed"]
    for i in range(12):
        accel = C["skidding_start_accel"] if vx > 0.0 else C["skidding_accel"]
        vx = max(-C["max_speed"], vx - accel * DT)
        x += vx * DT
        rows.append(frame(i, (x, 0.0, 0.0), (vx, 0.0, 0.0), "Skidding", grounded=True))
    return rows


def wall_jump() -> list[dict[str, object]]:
    rows = []
    x = z = 0.0
    vx = C["wall_jump_xy_speed"]
    vz = C["jump_speed"]
    for i in range(8):
        if i * DT >= C["jump_hold_time"]:
            vz -= C["gravity"] * DT
        x += vx * DT
        z += vz * DT
        rows.append(frame(i, (x, 0.0, z), (vx, 0.0, vz), "Normal", grounded=False, wall_contact=(i == 0)))
    return rows


def climb_up() -> list[dict[str, object]]:
    rows = []
    z = 0.0
    for i in range(8):
        vz = C["climb_speed"]
        z += vz * DT
        rows.append(frame(i, (0.0, 0.0, z), (0.0, 0.0, vz), "Climbing", grounded=False, climb_held=True))
    return rows


def platform_takeoff() -> list[dict[str, object]]:
    rows = []
    x = z = 0.0
    vx = 32.0
    vz = C["jump_speed"] + 24.0
    for i in range(8):
        if i * DT >= C["jump_hold_time"]:
            vz -= C["gravity"] * DT
        x += vx * DT
        z += vz * DT
        rows.append(
            frame(
                i,
                (x, 0.0, z),
                (vx, 0.0, vz),
                "Normal",
                grounded=False,
                platform_velocity=[32.0, 0.0, 24.0] if i == 0 else [0.0, 0.0, 0.0],
            )
        )
    return rows


def camera_vertical_dead_zone() -> list[dict[str, object]]:
    rows = []
    origin_z = 0.0
    for i, player_z in enumerate((0.0, 4.0, 8.0, 9.0, 16.0, 7.0, -2.0)):
        if player_z < origin_z:
            target_z = player_z
        elif player_z > origin_z + 8.0:
            target_z = player_z - 8.0
        else:
            target_z = origin_z
        origin_z += (target_z - origin_z) * (1.0 - math.pow(0.001, DT))
        rows.append(
            {
                "frame": i,
                "player_z": q(player_z),
                "camera_origin_z": q(origin_z),
                "state": "Normal",
            }
        )
    return rows


SCENARIOS: dict[str, tuple[str, Callable[[], list[dict[str, object]]], list[str]]] = {
    "jump_full_hold": ("jump", jump_full_hold, ["JumpSpeed", "JumpHoldTime", "Gravity"]),
    "dash_air": ("dash", dash_air, ["DashSpeed", "DashTime", "DashEndSpeedMult"]),
    "skid_reverse": ("skid", skid_reverse, ["MaxSpeed", "SkiddingStartAccel", "SkiddingAccel"]),
    "wall_jump": ("wall", wall_jump, ["WallJumpXYSpeed", "JumpSpeed", "JumpHoldTime"]),
    "climb_up": ("climb", climb_up, ["ClimbSpeed"]),
    "platform_takeoff": ("platform", platform_takeoff, ["platformVelocity", "AddPlatformVelocity"]),
    "camera_vertical_dead_zone": ("camera", camera_vertical_dead_zone, ["cameraOriginPos", "ZPad"]),
}


def document(name: str) -> dict[str, object]:
    family, builder, symbols = SCENARIOS[name]
    return {
        "schema_version": 1,
        "scenario_id": name,
        "family": family,
        "reference": {
            "source_file": SOURCE_FILE,
            "symbols": symbols,
            "fixed_dt": DT,
            "coordinate_system": "Celeste64 source units; +Z is up",
            "capture_mode": "source-derived model; C# game not executed",
        },
        "frames": builder(),
    }


def encoded(name: str) -> str:
    return json.dumps(document(name), indent=2, sort_keys=True) + "\n"


def regenerate(trace_dir: Path) -> int:
    trace_dir.mkdir(parents=True, exist_ok=True)
    for name in sorted(SCENARIOS):
        (trace_dir / f"{name}.json").write_text(encoded(name), encoding="utf-8")
    print(f"regenerated {len(SCENARIOS)} traces in {trace_dir}")
    return 0


def compare(trace_dir: Path) -> int:
    drifted = []
    for name in sorted(SCENARIOS):
        path = trace_dir / f"{name}.json"
        expected = encoded(name)
        actual = path.read_text(encoding="utf-8") if path.exists() else None
        if actual != expected:
            drifted.append(path.name)
    if drifted:
        print("trace drift:", ", ".join(drifted), file=sys.stderr)
        return 1
    print(f"matched {len(SCENARIOS)} traces in {trace_dir}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("compare", "regenerate"))
    parser.add_argument("--trace-dir", type=Path, default=DEFAULT_TRACE_DIR)
    args = parser.parse_args(argv)
    return regenerate(args.trace_dir) if args.command == "regenerate" else compare(args.trace_dir)


if __name__ == "__main__":
    raise SystemExit(main())
