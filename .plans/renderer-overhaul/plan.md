# Implementation plan

## Inc 1 — Ground-truth diagnostics (M)

**Depends on:** none
**Unblocks:** 3
**Files:** tests/, tools/, debug scene/render harness files
**Done when:** a room-scale known-good `.t3dm` fixture renders in our ROM with an expected screenshot/checksum, the stale `level_loader_test.cpp` fixture is repaired or replaced, and a host report for `1-1` reproduces current bake facts: source class counts, duplicate vertices, collinear triangles, winding status, and material budget.
**Risks:** if diagnostics are skipped, every later fix can still be aimed at the wrong layer.

## Inc 2 — Artifact + LVL1 compatibility contract (S)

**Depends on:** none
**Unblocks:** 4, 5
**Files:** docs/, room metadata/build config, plan-linked contract docs
**Done when:** the repo explicitly chooses LVL1 compatibility rather than LVL2 for this overhaul, separates gameplay `.lvl` data from visible `.t3dm` render data, defines two-axis brush policy slots, and requires no extra runtime material manifest for the new path.
**Risks:** a vague contract lets the old coupled design reappear under new names.
**Status:** done
**Attempts:** 1
**Files changed:** docs/room_artifact_contract.md, .plans/renderer-overhaul/*.md (matches plan: yes)
**Done-criteria check:** passed (evidence: /tmp/hawk-implement-plan-verify-inc2.log)
**Tests added/modified:** none

## Inc 3 — Map + baker audit tooling (M)

**Depends on:** 1
**Unblocks:** 6
**Files:** tools/bake_map.py, new validation/report tooling, tests/bake_* or geometry tests
**Done when:** `1-1` produces a deterministic report plus host-side debug export that classifies brush-bearing entities on both policy axes, rejects/cleans malformed geometry, and emits an explicit decision artifact: `map-derived render mesh viable` or `re-author first room`.
**Risks:** overfitting the baker to one room can preserve bad source semantics at scale.

## Inc 4 — Native `.t3dm` room renderer (M)

**Depends on:** 2
**Unblocks:** 5, 6
**Files:** src/user/gameplay/render/, src/user/gameplay/scene/, Makefile as needed
**Done when:** a static room `.t3dm` is loaded and drawn through tiny3d's native model path (`t3d_model_draw` is sufficient unless later visibility work needs object iteration), with no custom face-at-a-time room draw loop in the new path.
**Risks:** display-list recording and material state must remain correct across room and actor draws.

## Inc 5 — Render-artifact validation (M)

**Depends on:** 2, 4
**Unblocks:** 6
**Files:** tools/, tests/, Makefile/build rules as needed
**Done when:** the new `.t3dm` render path has deterministic geometry/material validation, including TMEM checks derived from render-artifact material references rather than the legacy `.manifest`.
**Risks:** if validation stays legacy-only, the previous TMEM failure can silently return under the new renderer.

## Inc 6 — First-room render asset pipeline (L)

**Depends on:** 3, 4, 5
**Unblocks:** 7
**Files:** assets/, tools/, Makefile, filesystem generation rules, build reports
**Done when:** the route chosen by Inc 3 is executed for `1-1`, the resulting render artifact passes validators, and Ares shows an intact room silhouette with no needles, accidental helper geometry, or missing textures.
**Risks:** this is where the plan must obey the earlier route decision instead of reopening it mid-implementation.

## Inc 7 — Scene cutover + legacy-path quarantine (M)

**Depends on:** 6
**Unblocks:** none
**Files:** gameplay scene, loader integration, legacy renderer quarantine, docs/tests
**Done when:** gameplay uses `.lvl` for logic and `.t3dm` for room visuals, host checks pass, Ares boots cleanly, shipping room rendering no longer consumes LVL1 face fans, and the old path is retained only behind an explicit debug switch.
**Risks:** deleting compatibility payloads too early would turn a renderer plan into an unplanned schema migration.
