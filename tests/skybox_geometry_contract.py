#!/usr/bin/env python3
"""Catch skybox triangle-index and far-plane regressions."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

source = (ROOT / "src/user/gameplay/render/skybox.cpp").read_text()
assert "SkyboxTriangleForFace(f, triangle_index)" in source, "draw must use tested face indices"
assert "t3d_tri_draw(triangle.a, triangle.b, triangle.c);" in source
assert "kSkyboxHalfExtent" in source, "skybox scale must stay within near-pass range"
assert "if (!SkyboxFitsFarPlane(cam.far)) return;" in source

math_source = (ROOT / "src/user/gameplay/render/skybox_math.hpp").read_text()
assert "kSkyboxHalfExtent = 350.0f" in math_source
assert "SkyboxFitsFarPlane" in math_source

host_test = (ROOT / "tests/skybox_transform.cpp").read_text()
assert "SkyboxTriangleForFace" in host_test
assert "SkyboxFitsFarPlane(800.0f)" in host_test
print("skybox_geometry_contract: PASS")
