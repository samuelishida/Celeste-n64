# Implementation Plan

## Inc 1 — DFS + bake wiring (S)

**Depends on:** none
**Unblocks:** 2
**Files:** Makefile, tools/bake_map.py, .gitignore
**Done when:** `make` and `python3 tests/bake_map_smoke.py` pass; Ares logs the baked `1-1` load, six manifest materials, and the expected spawn. Evidence: `/tmp/inc1-lrw-verify.log`.
**Risks:** generated level artifacts stay ignored; baker material IDs must match manifest order.
**Status:** done
**Attempts:** 3 stages: fixed nondeterministic remap + bad Makefile outputs, then captured Ares serial evidence with user help
**Files changed:** Makefile, tools/bake_map.py, .gitignore (matches plan: yes)
**Done-criteria check:** passed (evidence: /tmp/inc1-lrw-verify.log)
**Tests added/modified:** none

## Inc 2 — TMEM-safe material derivatives (M)

**Depends on:** 1
**Unblocks:** 3
**Files:** tools/convert_og_assets.py, docs/asset_budget.md
**Done when:** the converter emits `32x32 RGBA16` derivatives for every first-room material and the room no longer depends on oversized source-resolution textures.
**Risks:** downscaling `snow_1` and `floor_dirty_concrete` reduces fidelity; preserve logical names so maps do not change.
**Status:** done
**Attempts:** 1
**Files changed:** tools/convert_og_assets.py, docs/asset_budget.md (matches plan: yes)
**Done-criteria check:** passed (evidence: /tmp/hawk-implement-plan-verify-inc2.log)
**Tests added/modified:** none

## Inc 3 — Material budget validation (S)

**Depends on:** 2
**Unblocks:** 4
**Files:** tests/material_budget_smoke.py
**Done when:** `python3 tests/material_budget_smoke.py` fails on any manifest material that is missing or not TMEM-safe, and passes for the conditioned `1-1` set.
**Risks:** the test must inspect the actual manifest-driven material set, not a stale hand-written fixture.
**Status:** done
**Attempts:** 2 stages: direct compressed-sprite parsing was invalid, switched the conditioned subset to uncompressed inspectable sprites
**Files changed:** tests/material_budget_smoke.py (matches plan: yes)
**Done-criteria check:** passed (evidence: /tmp/hawk-implement-plan-verify-inc3.log)
**Tests added/modified:** tests/material_budget_smoke.py

## Inc 4 — Textured LevelRenderer (M)

**Depends on:** 3
**Unblocks:** none
**Files:** src/user/gameplay/render/level_renderer.hpp, src/user/gameplay/render/level_renderer.cpp, src/user/gameplay/scene/gameplay_scene.cpp
**Done when:** Ares shows at least two distinct TMEM-safe materials, lit faces differ from shadowed faces, graybox fallback remains mid-grey, and frame time is `<= 33 ms`. Evidence: `/tmp/inc4-lrw-verify.log`.
**Risks:** if per-face uploads are too slow, record the miss and follow with material-group batching rather than widening this increment.
