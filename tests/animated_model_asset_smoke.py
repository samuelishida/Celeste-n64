#!/usr/bin/env python3
"""Offline qualification of Madeline skeleton, clips, and T3DM output."""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    report = json.loads((ROOT / "build/n64-prepared/player.report.json").read_text())
    manifest = json.loads((ROOT / "assets/og_converted/asset_manifest.json").read_text())
    asset = next(a for a in manifest["assets"] if a["id"] == "player")
    clips = {name for name in report["clip_names"]}
    required = {item["source"] for item in asset["animations"]}
    assert required <= clips, f"missing clips: {sorted(required - clips)}"
    assert report["animated"] and report["joint_count"] == 17
    assert report["image_dependencies"] == []
    assert report["skin_source_vertex_ids"] == {
        "attribute": "TEXCOORD_0.x",
        "packed_s_offset": 0,
        "packed_s_units_per_id": 1,
        "source_vertex_count": report["vertex_count"],
    }
    normalization = report["normalized_skin_root"]
    assert normalization["changed"]
    assert normalization["compensated_joint_roots"]
    assert normalization["adjusted_translation_accessors"]
    assert any(abs(root["offset"][0] - 0.1236680299) < 1e-7
               for root in normalization["roots"]), \
        "source Armature translation was not reported as migrated"
    assert len(normalization["adjusted_translation_accessors"]) == 13
    output = ROOT / "assets/og_converted/models/player.t3dm"
    data = output.read_bytes()
    assert data[:3] == b"T3M" and len(data) > 1024
    # tiny3d T3DM uses chunk type 'A' for animation definitions; at least one
    # animation chunk proves converter did not silently flatten the skeleton.
    assert b"A" in data, "player T3DM has no animation chunk marker"
    runtime = (ROOT / "src/user/gameplay/render/animated_model.cpp").read_text()
    assert "SampleAnimationCurve(curve, animation_curve_keys_" in runtime
    assert "t3d_anim_create" not in runtime and "t3d_anim_update" not in runtime, \
        "exact PAS1 curves must be authoritative; do not sample quantized Tiny3D streams"
    prepare_draw = runtime.split("bool AnimatedModel::PrepareDraw()", 1)[1].split(
        "void AnimatedModel::Draw()", 1)[0]
    assert "UpdateWeightedSkinVertices" not in prepare_draw, \
        "stable profile must not CPU-skin every packed vertex per frame"
    assert "t3d_model_draw_skinned(model_, &skeleton_)" in runtime, \
        "stable profile must use Tiny3D's buffered joint-skin draw"
    draw = runtime.split("void AnimatedModel::Draw() const", 1)[1].split(
        "const char* AnimatedModel::CurrentClipName()", 1)[0]
    assert "t3d_skeleton_use(&skeleton_)" in draw, \
        "buffered Tiny3D skin draw must bind active skeleton matrix buffer first"
    assert draw.index("t3d_skeleton_use(&skeleton_)") < draw.index(
        "t3d_model_draw_skinned(model_, &skeleton_)"), \
        "buffered Tiny3D skin draw must bind active skeleton matrix buffer first"
    print(f"animated_model_asset_smoke: PASS ({len(report['clip_names'])} clips, {report['joint_count']} joints)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
