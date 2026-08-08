# Convert OG 1-1 Map to Colmesh Pipeline

## Context

The project currently has two room sources:
- **`first-room`** — hand-authored TrenchBroom map, fully working with our `.lvl` + `.colmesh` pipeline
- **`1-1`** — imported OG Celeste64 Quake `.map`, partially baked but with shell gaps, reversed winding artifacts, and entity classes that don't map to our actor system

The OG `1-1.map` uses Quake-style brush geometry with entity classes (`SpikeBlock`, `TrafficBlock`, `DeathBlock`, `func_group`, `Cassette`, `Decoration`) that have no runtime equivalent. The current bake pipeline (`bake_map.py` → `.lvl`, `colmesh_bake.py` → `.colmesh`) can process the file but produces a broken shell because:
1. Some OG brushes use `TB_empty` texture (death plane geometry) which should be filtered
2. Polygon winding was reversed in early bakes (now fixed but 1-1 needs re-bake)
3. Entity dispatch doesn't recognize OG classes like `Node`, `TrafficBlock`, `Cassette`

The goal is a faithful conversion: preserve the original level layout and challenge beats while mapping to our collision + actor system.

## Architectural decisions

- **Decision:** Keep the existing two-stage pipeline (`bake_map.py` → `.lvl` + `colmesh_bake.py` → `.colmesh`). Do not merge them. Rationale: the separation allows independent testing of geometry vs collision, and the Makefile pattern rules already handle the dependency chain correctly.
- **Decision:** Filter OG entity classes at parse time in `bake_map.py`. Only emit brushes from whitelisted classes (`worldspawn`, `func_wall`, `func_climbable`). Skip `Decoration`, `SpikeBlock`, `TrafficBlock`, `DeathBlock`, `func_group`, `Cassette`, `Node` entirely. Rationale: these map to gameplay systems we don't yet support (traffic blocks, music cues, node-based pathing).
- **Decision:** Map `PlayerSpawn` origin from Quake space to game space using the existing transform `(x*0.2, -z*0.2, -y*0.2)`. Rationale: this is already verified in `first-room`; no new coordinate convention needed.
- **Decision:** Use `_climbable` suffix on rock textures for wall-climb surfaces. Rationale: the runtime `FaceIsClimbable` check already reads material flags from `.colmesh`; we just need the texture name to end with `_climbable`.
- **Decision:** Do not add new entity classes to `entity_ids.hpp` for OG-specific types (`Node`, `TrafficBlock`). Instead, log and skip them during bake. Rationale: these are Celeste64-specific pathing/music entities that don't map to our movement-first prototype.

## Assumptions and answers from code

- **Assumption:** The transform `(x*0.2, -z*0.2, -y*0.2)` is the canonical Quake→game convention. Source: `tools/bake_map.py:transform_point` — confirmed working in `first-room`.
- **Assumption:** BVH depth limit of 30 is sufficient for 1-1 geometry. Source: `tools/colmesh_bake.py:MAX_DEPTH = 30` — the OG map has ~50 brushes but most are simple boxes; estimated triangle count < 2000, well within BVH capacity.
- **Answer from code:** `VISUAL_ONLY_BRUSH_CLASSES` already exists in `bake_map.py` and includes `Decoration`. Source: `tools/bake_map.py` — confirmed.
- **Answer from code:** `UNSUPPORTED_BRUSH_CLASSES` already exists and includes `SpikeBlock`, `TrafficBlock`, `DeathBlock`, `func_group`, `Cassette`. Source: `tools/bake_map.py` — confirmed.
- **Answer from code:** The 1-1 map uses `TB_empty` texture on death-plane brushes (entity 55, `DeathBlock`). These should be filtered by the existing `UNSUPPORTED_BRUSH_CLASSES` check. Source: `Celeste64-og/Content/Maps/1-1.map` — entity 55 has `TB_empty` faces.
- **Answer from code:** `entity_dispatch.cpp` maps LVL classname_id → placeholder_id for `Strawberry`, `Refill`, `Spring`. No mapping exists for `Node`, `TrafficBlock`, `Cassette`. Source: `src/user/gameplay/world/entity_dispatch.cpp` — confirmed.

## Risks accepted

- **Risk:** OG map scale may not match our game units (0.2 multiplier). Mitigation: verify spawn Y matches platform top; adjust if needed.
- **Risk:** BVH build time for dense 1-1 geometry could exceed limits. Mitigation: monitor triangle count; split into sub-scenes if >4096 vertices.
- **Risk:** Some OG brush winding may still produce degenerate triangles. Mitigation: the existing `level_bake_report_smoke.py` guardrails catch this.

## Increment DAG

- Inc 1 — Audit & Filter (S) — depends on: none — unblocks: 2, 3
- Inc 2 — Coordinate Transform Verification (S) — depends on: 1 — unblocks: 4
- Inc 3 — Entity Mapping & Spawn Points (M) — depends on: 1 — unblocks: 4
- Inc 4 — Full Bake + Colmesh (M) — depends on: 2, 3 — unblocks: 5a, 5b
- Inc 5a — Spawn & Basic Movement (M) — depends on: 4 — unblocks: none
- Inc 5b — Collectibles & Respawn (S) — depends on: 4 — unblocks: none

## Increments

### Inc 1 — Audit & Filter OG Map Classes (S)
**Depends on:** none
**Unblocks:** 2, 3
**Done criteria:** `bake_map.py` skips all unsupported OG classes and logs them; only whitelisted brushes produce output.

#### Files to touch

##### tools/bake_map.py
- What changes: Add `Node` to the existing `UNSUPPORTED_BRUSH_CLASSES` set (func_group is already listed). Add a log line for each skipped entity class so the bake console shows what was dropped.
- Function(s): `_parse_entity_block` — add early return for unsupported classes before brush processing
- Data shapes: None (filtering logic only)
- Error paths: If an entity has no brushes, skip silently; if all faces use `TB_empty`, log a warning

##### .agents/common-mistakes/og-map-polygon-winding.md
- What changes: Add a new section documenting the 1-1 conversion scope and the filter policy for OG classes.

#### Edge cases
- Some OG brushes mix supported textures (`rock_1`) with `TB_empty` — only emit faces with non-empty textures.
- `func_group` entities (entity 54 in 1-1) are layer containers, not geometry — skip entirely.

#### Verification
- Run: `python3 tools/bake_map.py Celeste64-og/Content/Maps/1-1.map /tmp/1-1-audit.lvl /tmp/1-1-audit.manifest`
- Check console for: `[bake] skipped unsupported class=Node`, `[bake] skipped unsupported class=TrafficBlock`, etc.
- Check output: `.lvl` should contain only `worldspawn` brushes

### Inc 2 — Coordinate Transform Verification (S)
**Depends on:** Inc 1
**Unblocks:** 4
**Done criteria:** Spawn point and platform Y-coordinates match expected game-space values after transform.

#### Files to touch

##### tools/bake_map.py
- What changes: Verify the existing `transform_point` function produces correct game-space coordinates for 1-1 spawn. Add a debug dump of transformed spawn positions.
- Function(s): `transform_point` — no change needed; just verify output
- Data shapes: Debug log of `(quake_x*0.2, -quake_z*0.2, -quake_y*0.2)` for each `PlayerSpawn`

##### tests/fixtures/1-1.manifest
- What changes: Update to reflect the filtered shell-only bake (remove non-shell materials like `floor_dirty_concrete`, `metal_floor_1`).

#### Edge cases
- The OG 1-1 spawn is at `origin "32 120 384"` — after transform: `(6.4, +76.8, -24.0)`. Verify this lands on a platform.
- Some platforms are at Z=304 in Quake space → Y=+60.8 in game space. Verify these match expected floor heights.

#### Verification
- Run: `python3 tools/bake_map.py Celeste64-og/Content/Maps/1-1.map /tmp/1-1-transform.lvl /tmp/1-1-transform.manifest`
- Check debug output for spawn transform
- Compare with `first-room` spawn Y (~384 → 76.8) to verify scale consistency

### Inc 3 — Entity Mapping & Spawn Points (M)
**Depends on:** Inc 1
**Unblocks:** 4
**Done criteria:** `PlayerSpawn` entity produces correct spawn in `.lvl`; unsupported entities are logged but don't crash the baker.

#### Files to touch

##### tools/bake_map.py
- What changes: Ensure `PlayerSpawn` classname is recognized and its origin is transformed + stored in the LVL entity table. Add handling for `Strawberry` entities (entity 18 in OG map).
- Function(s): `_parse_entity_block` — add classname extraction for `PlayerSpawn` and `Strawberry`
- Data shapes: LVL entity table entries with transformed positions

##### src/user/gameplay/world/entity_dispatch.cpp
- What changes: Verify `ClassnameToPlaceholder` correctly maps `kEntStrawberry=1` → placeholder 2. No new entity types needed — OG-specific types (`Node`, `TrafficBlock`) are skipped at bake time.
- Function(s): None (verification only)

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: Add `#ifndef DEFAULT_LEVEL_PATH` guard around `kBakedLevelPath`. Default to `"rom:/lvl/first-room.lvl"`; Inc 5 passes `-DDEFAULT_LEVEL_PATH='"rom:/lvl/1-1.lvl"'` via Makefile when building for ROM validation.
- Function(s): None (config change)

#### Edge cases
- OG 1-1 HAS an explicit Strawberry entity (entity 18) at origin `"16 3696 456"` — transforms to `(3.2, +91.2, -739.2)`. Verify this is reachable from spawn or needs repositioning.
- The OG map has `Cassette` entities (music cues) — skip these; they have no runtime equivalent.

#### Verification
- Run: `python3 tools/bake_map.py Celeste64-og/Content/Maps/1-1.map /tmp/1-1-spawn.lvl /tmp/1-1-spawn.manifest`
- Check `.lvl` entity table for `PlayerSpawn` and any `Strawberry` entries
- Verify transformed coordinates are reasonable

### Inc 4 — Full Bake + Colmesh (M)
**Depends on:** Inc 2, Inc 3
**Unblocks:** 5
**Done criteria:** `1-1.lvl` and `1-1.colmesh` bake successfully; `level_bake_report_smoke.py` reports zero degenerate faces.

#### Files to touch

##### Makefile
- What changes: Add `1-1.lvl`, `1-1.manifest`, and `1-1.colmesh` to the DFS file lists. Ensure the pattern rule for `.colmesh` generation covers `1-1`.
- Function(s): None (build config)

##### tools/colmesh_bake.py
- What changes: Verify the existing material flag logic correctly maps `rock_1`, `snow_1`, `rock_2` textures to `MAT_SOLID`. No suffix = solid. This should already work.
- Function(s): `material_flags` — verify default case returns `MAT_SOLID`

##### src/user/gameplay/scene/gameplay_scene.cpp
- What changes: No change needed here — the `#ifndef DEFAULT_LEVEL_PATH` guard was added in Inc 3. Inc 4 just builds ROM with `-DDEFAULT_LEVEL_PATH='"rom:/lvl/1-1.lvl"'`.

#### Edge cases
- The OG map has ~50 brushes; estimated triangle count ~800-1200. Verify BVH build completes within depth 30.
- Some faces use `floor_dirty_concrete` texture — these come from `SpikeBlock` entities which are filtered. Verify no stray faces slip through.

#### Verification
- Run: `python3 tools/bake_map.py Celeste64-og/Content/Maps/1-1.map filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest`
- Run: `python3 tools/colmesh_bake.py filesystem/lvl/1-1.lvl filesystem/lvl/1-1.colmesh`
- Run: `python3 tests/level_bake_report_smoke.py` — expect `duplicate_vertex_faces=0`, `first_fan_degenerate_faces=0`, `reversed_winding_faces=0`
- Build ROM: `./compile-rom.sh`

### Inc 5a — Spawn & Basic Movement (M)
**Depends on:** Inc 4
**Unblocks:** none
**Done criteria:** Player spawns at correct position on 1-1 platform; walking, jumping, and basic collision work.

#### Files to touch

##### src/user/gameplay/player/player_motor.cpp
- What changes: Tune gravity/jump force if the transformed scale feels off. The 0.2 multiplier may make platforms feel "far apart" — verify with ROM.
- Function(s): `PlayerMotor::Update` — adjust constants if needed

##### src/user/gameplay/physics/coll_mesh.cpp
- What changes: Debug BVH traversal if collision queries fail on 1-1 geometry. Add logging for raycast misses.
- Function(s): `Raycast`, `SweepSphere` — add debug output

#### Edge cases
- If the spawn point is inside a wall (transform error), adjust `PlayerSpawn` origin in the map or add a spawn validation step.
- If collision feels "sticky" on rock textures, verify material flags are correctly set to `MAT_SOLID`.

#### Verification
- Boot ROM in Mupen64Plus or Ares
- Test: player spawns on platform, can walk/jump/dash
- Test: fall below kill plane → respawn at spawn

### Inc 5b — Collectibles & Respawn (S)
**Depends on:** Inc 4
**Unblocks:** none
**Done criteria:** Strawberry pickup works; respawn resets dash state correctly.

#### Files to touch

##### src/user/gameplay/world/collectible.cpp
- What changes: Verify AABB overlap detection works with transformed strawberry position `(3.2, +91.2, -739.2)`.
- Function(s): `Collectible::Update` — verify pickup radius

##### src/user/gameplay/world/respawn_system.cpp
- What changes: Verify respawn resets dash state after collecting strawberry.
- Function(s): `RespawnSystem::Reset` — verify dash refill

#### Verification
- Test: collect strawberry at transformed position
- Test: fall off → respawn with dash refilled
- If the spawn point is inside a wall (transform error), adjust `PlayerSpawn` origin in the map or add a spawn validation step.
- If collision feels "sticky" on rock textures, verify material flags are correctly set to `MAT_SOLID`.

#### Verification
- Boot ROM in Mupen64Plus or Ares
- Test: player spawns on platform, can walk/jump/dash
- Test: collect strawberry at transformed position
- Test: fall below kill plane → respawn at spawn
- Test: wall-climb on `_climbable` surfaces (if any rock textures are tagged)

## Cross-cutting verification

After Inc 5, manually verify the full movement loop on 1-1:
1. Spawn → walk across main platform
2. Jump to upper platforms
3. Dash across gaps
4. Collect strawberry
5. Fall off → respawn

## Standards / common-mistakes referenced
- `.agents/common-mistakes/og-map-polygon-winding.md` — reversed winding fix already applied; 1-1 needs re-bake
- `docs/colmesh_format.md` — BVH layout, material flags, quantization
- `docs/first-room-brief.md` — brush-class policy, material suffix contract

## Open questions (CONSIDER from review)
- Should we add a `_climbable` texture variant for the OG rock textures, or tag specific brushes in TrenchBroom?
- Does the OG 1-1 have enough verticality to justify the conversion, or is `first-room` sufficient for Milestone 2?
- Should we preserve the OG node/path entities for future pathing system integration, or discard them entirely?

## Out of scope
- Converting OG models (`tree1.glb`, `bush1.glb`, etc.) — these are handled separately in the asset pipeline
- Adding traffic block or spike block gameplay — out of scope for movement-first prototype
- Multi-room level transitions — only single-room loading is targeted
- Audio/music entity conversion — Celeste64 music cues have no runtime equivalent yet
