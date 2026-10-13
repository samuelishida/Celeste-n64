#!/usr/bin/env python3
"""Pin actor-update broadphase inputs to the checked-in OG camera source."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GAME = (ROOT / "Celeste64-og/Source/Game.cs").read_text(encoding="utf-8-sig")
CAMERA = (ROOT / "Celeste64-og/Source/Graphics/Camera.cs").read_text(encoding="utf-8-sig")
WORLD = (ROOT / "Celeste64-og/Source/Scenes/World.cs").read_text(encoding="utf-8-sig")
VIEW = (ROOT / "src/user/gameplay/world/actor_update_view.hpp").read_text()


def main() -> None:
    assert "public const int Width = 640;" in GAME
    assert "public const int Height = 360;" in GAME
    assert "private readonly Target target = new(Width, Height," in GAME
    assert "var width = target?.Width ?? App.WidthInPixels;" in CAMERA
    assert "var height = target?.Height ?? App.HeightInPixels;" in CAMERA
    assert "MathF.PI / 4.0f * fovMultiplier, width / (float)height" in CAMERA
    assert "Camera.NearPlane = 20;" in WORLD
    assert "Camera.FarPlane = 800;" in WORLD
    assert "Camera.Frustum.GetBoundingBox().Inflate(10)" in WORLD
    assert "Camera.Target = target;" in WORLD
    assert "kAspectRatio = 16.0f / 9.0f" in VIEW
    assert "kVerticalFovDegrees = 45.0f" in VIEW
    assert "kNearPlane = 20.0f" in VIEW
    assert "kFarPlane = 800.0f" in VIEW
    assert "kInflation = 10.0f" in VIEW
    print("PASS: actor_update_view_source_smoke")


if __name__ == "__main__":
    main()
