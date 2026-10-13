#!/usr/bin/env python3
"""Animated flat-color player packages rig streams and no runtime atlas."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from asset_manifest import validate_manifest


def main() -> int:
    manifest_path = ROOT / "assets/og_converted/asset_manifest.json"
    manifest = json.loads(manifest_path.read_text())
    diagnostics = validate_manifest(manifest)
    assert not diagnostics, "manifest invalid: " + "; ".join(diagnostics)
    player = next(item for item in manifest["assets"] if item["id"] == "player")
    texture = next(item for item in manifest["assets"] if item["id"] == "player_texture")
    animmap = next(item for item in manifest["assets"] if item["id"] == "player_animmap")
    assert player["profile"] == "animated_model" and player["dependencies"] == []
    assert player["status"] == "active" and player["required"] and player["package"]
    assert texture["status"] == "planned" and texture["required"] is False and texture["package"] is False
    assert animmap["status"] == "active" and animmap["source_asset"] == "player"
    assert animmap["dependencies"] == ["player"]
    assert (ROOT / "filesystem/mdl/player.t3dm").is_file()
    assert (ROOT / "filesystem/mdl/player.animmap").is_file()
    curve_sidecars = [output for output in player["outputs"] if output["format"] == "animation_curve_sidecar"]
    assert len(curve_sidecars) == 1 and curve_sidecars[0]["path"].endswith("player.anim")
    assert (ROOT / curve_sidecars[0]["path"]).is_file()
    assert (ROOT / curve_sidecars[0]["package_path"]).is_file()
    assert not (ROOT / "filesystem/tex/player.sprite").exists()
    sidecars = [output for output in player["outputs"] if output["path"].endswith(".sdata")]
    assert len(sidecars) == 11
    assert all((ROOT / output["path"]).is_file() for output in sidecars)
    skin_sidecars = [output for output in player["outputs"] if output["format"] == "weighted_skin_sidecar"]
    assert len(skin_sidecars) == 1 and skin_sidecars[0]["path"].endswith("player.wsk")
    assert (ROOT / skin_sidecars[0]["path"]).is_file()
    assert (ROOT / skin_sidecars[0]["package_path"]).is_file()
    assert [item["package_path"] for item in player["outputs"] if item["format"] == "t3dm"] == ["filesystem/mdl/player.t3dm"]
    dry_run = subprocess.run(["make", "-n", "madeline_cube_rom.dfs"], cwd=ROOT, check=True, capture_output=True, text=True).stdout
    generated = (ROOT / "build/og_assets.mk").read_text()
    package_commands = dry_run + generated
    assert "filesystem/mdl/player.t3dm" in package_commands
    assert "filesystem/mdl/player.animmap" in package_commands
    assert "filesystem/mdl/player.10.sdata" in package_commands
    assert "filesystem/mdl/player.wsk" in package_commands
    assert "filesystem/mdl/player.anim" in package_commands
    assert "filesystem/tex/player.sprite" not in package_commands
    print("player animation/flat-color packaging smoke: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
