# Implementation plan

## Inc 1 — Room brief + source contract (S)

**Depends on:** none
**Unblocks:** 2, 3
**Files:** `docs/`, `assets/rooms/first-room/`, `tests/fixtures/`
**Done when:** a checked-in brief names the four Milestone 2 beats, room id, authored source paths, `PlayerSpawn = checkpoint` policy, existing-runtime kill-plane policy, collision material suffix contract, material budget, and six shared anchors used by later tests.
**Risks:** if the room brief stays implicit, later geometry changes will optimize for screenshots instead of play.

**Status:** done
**Attempts:** 1
**Files changed:** docs/first-room-brief.md, tests/fixtures/first-room-anchors.json, assets/rooms/first-room/ (created dir) (matches plan: yes)
**Done-criteria check:** passed (evidence: /tmp/hawk-implement-plan-verify-inc1.log)
**Tests added:** none (content-only increment)

## Inc 2 — Gameplay source + collision artifacts (M)

**Depends on:** 1
**Unblocks:** 4
**Files:** `assets/rooms/first-room/first-room.map`, `tools/bake_map.py`, `tools/colmesh_bake.py`, `Makefile`, `tests/`
**Done when:** the project-owned map bakes to `first-room.lvl` + `first-room.colmesh`, only declared gameplay brush classes emit gameplay data, `_climbable` wall material emits `MAT_CLIMBABLE`, `python3 tests/colmesh_smoke.py filesystem/lvl/first-room.colmesh` passes, and host queries find floor under spawn plus the declared climb wall.
**Risks:** reusing the current all-brush bake rule would smuggle unwanted solids back into the new room.

**Status:** done
**Attempts:** 1
**Files changed:** assets/rooms/first-room/first-room.map, tools/bake_map.py, tools/colmesh_bake.py, tests/first_room_query_test.cpp (matches plan: yes; extra: tools/colmesh_bake.py — manifest-based material name fix for climbable flag resolution)
**Done-criteria check:** passed (evidence: /tmp/hawk-implement-plan-verify-inc2.log)
**Tests added:** tests/first_room_query_test.cpp (5 checks: floor, climb tris, wall raycast, landing, SurfaceOwnerOf)

## Inc 3 — Render source + `.t3dm` artifact (M)

**Depends on:** 1
**Unblocks:** 4
**Files:** `assets/rooms/first-room/first-room.glb`, conversion/build rules, render validators, anchor-export tooling, `filesystem/lvl/first-room.t3dm`
**Done when:** the render source converts deterministically to `first-room.t3dm`, exports the six named anchors to a sidecar fixture, host-side render-artifact validation passes, material validation passes, and an Ares capture shows a coherent room silhouette with no needles or missing surfaces.
**Risks:** this must not reopen the OG fidelity bargain through the back door.

## Inc 4 — Cross-artifact acceptance fixtures (M)

**Depends on:** 2, 3
**Unblocks:** 5
**Files:** `tests/`, `tests/fixtures/`, room validation tooling
**Done when:** host checks verify spawn clearance, floor-under-spawn, dash-gap landing, climb-wall contact, collectible-route reachability, kill-drop respawn, and anchor agreement between gameplay and render sources.
**Risks:** without cross-artifact tests, the room can be “correct” twice and still be wrong once assembled.

## Inc 5 — Runtime cutover to `first-room` (M)

**Depends on:** 4 + renderer-overhaul Inc 7
**Unblocks:** 6
**Files:** `src/user/gameplay/scene/gameplay_scene.cpp`, `Makefile`, DFS packaging, runtime smoke tests
**Done when:** the ROM boots into `first-room`, loads `.lvl/.colmesh/.t3dm` side-by-side, host smoke tests pass, and an Ares playthrough completes the four room beats without falling through or colliding with invisible geometry.
**Risks:** cutting over before the renderer plan is complete can hide actors or revive the legacy room path.

## Inc 6 — Retire imported-room dependency (S)

**Depends on:** 5
**Unblocks:** none
**Files:** `Makefile`, docs, tests, generated-artifact references
**Done when:** the shipping build no longer depends on `assets/og_converted/maps/1-1.map` or boots from imported `1-1` artifacts, while OG assets remain reference-only and rollback still has a documented switchback path.
**Risks:** deleting reference material instead of shipping dependencies would burn useful archaeology.
