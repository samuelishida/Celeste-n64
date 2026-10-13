#!/usr/bin/env python3
"""Guard persistent per-actor Tiny3D matrix storage for dynamic solids."""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = (ROOT / "src/user/gameplay/render/dynamic_solid_renderer.hpp").read_text()
SOURCE = (ROOT / "src/user/gameplay/render/dynamic_solid_renderer.cpp").read_text()

assert "T3DMat4FP* actor_matrices_ = nullptr;" in HEADER
assert "sizeof(T3DMat4FP) * geometry.ActorCount()" in SOURCE
assert "T3DMat4FP& model_fp = actor_matrices_[a];" in SOURCE
assert "t3d_matrix_push(&model_fp);" in SOURCE
assert "T3DMat4FP model_fp;" not in SOURCE, (
    "stack matrix can be overwritten while queued RSP DMA still reads it"
)
assert "if (actor_matrices_) free_uncached(actor_matrices_);" in SOURCE
print("dynamic_solid_matrix_lifetime_contract: persistent actor matrices PASS")
