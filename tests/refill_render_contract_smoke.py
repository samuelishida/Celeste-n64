#!/usr/bin/env python3
"""Guard refill model packaging and per-instance render lifecycle."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "src/user/gameplay/scene/gameplay_scene.cpp").read_text()
ACTOR = (ROOT / "src/user/gameplay/actor/refill_actor.hpp").read_text()
MANIFEST = json.loads((ROOT / "assets/og_converted/asset_manifest.json").read_text())


def check_asset(asset_id: str, filename: str) -> None:
    entry = next(item for item in MANIFEST["assets"] if item["id"] == asset_id)
    assert entry["required"] and entry["package"]
    assert entry["outputs"][0]["package_path"] == f"filesystem/mdl/{filename}"
    assert (ROOT / entry["outputs"][0]["path"]).is_file()
    assert (ROOT / "filesystem/mdl" / filename).is_file()


def main() -> None:
    check_asset("refill_gem", "refill_gem.t3dm")
    check_asset("refill_gem_double", "refill_gem_double.t3dm")

    for path in ("rom:/mdl/refill_gem.t3dm", "rom:/mdl/refill_gem_double.t3dm"):
        assert path in SCENE
    assert "refill_model.Load(kRefillModelPath)" in SCENE
    assert "double_refill_model.Load(kDoubleRefillModelPath)" in SCENE
    assert "impl_->refill_model.Free()" in SCENE
    assert "impl_->double_refill_model.Free()" in SCENE
    assert "ForEachType<RefillActor>" in SCENE
    assert "if (!refill.active || refill.collected) return;" in SCENE
    assert "refill.IsDouble()" in SCENE
    assert "refill.VisualPosition()" in SCENE
    assert "refill.VisualSpinYawRadians()" in SCENE
    assert "kRefillModelScale = 2.0f / 64.0f" in SCENE
    assert "kVisualBobAmplitude = 2.0f" in ACTOR
    assert "kVisualBobRate = 2.0f" in ACTOR
    assert "kVisualSpinRate = 3.0f" in ACTOR

    hitstop_gate = SCENE.index("ConsumeWorldHitstopTick")
    hitstop_skip = SCENE.index("continue;", hitstop_gate)
    update_view = SCENE.index("BuildActorUpdateView", hitstop_gate)
    input_tick = SCENE.index("input_system.BeginTick", hitstop_gate)
    first_source_pass = SCENE.index("actor_world.BeginSourceOrderedPass(false,")
    player_slot = SCENE.index("if (player_update_ran && defer_camera_input)")
    player_camera_input = SCENE.index("camera_controller.InputPhase", player_slot)
    player_update = SCENE.index("TimerInputPhase", player_camera_input)
    late_source_pass = SCENE.index("actor_world.BeginSourceOrderedPass(true,")
    assert hitstop_gate < hitstop_skip < update_view < input_tick < first_source_pass
    assert first_source_pass < player_slot < player_camera_input < player_update
    assert player_camera_input < late_source_pass
    camera_follow = SCENE.index("camera_controller.FollowPhase")
    pickup_dispatch = SCENE.index("ProcessRefillPickups")
    actor_update = SCENE.index("actor_world.Update", pickup_dispatch)
    hitstop_commit = SCENE.index("CommitWorldHitstopRequest", actor_update)
    assert camera_follow < pickup_dispatch < actor_update < hitstop_commit
    assert "AdvanceDynamicSolidBaseUpdate" in SCENE[first_source_pass:player_slot]
    assert 'std::strncmp(spawn.classname, "Cassette"' in SCENE
    assert "DispatchSourceOrderedActorSlot" in SCENE
    assert "cassette_actor.TryPickup" in SCENE
    assert "&actor_update_view" in SCENE[first_source_pass:late_source_pass]
    assert "&actor_update_view" in SCENE[late_source_pass:hitstop_commit]
    assert "simulation_ticks_this_frame" in SCENE
    assert "tick < impl_->simulation_ticks_this_frame" in SCENE
    print("PASS: refill_render_contract_smoke")


if __name__ == "__main__":
    main()
