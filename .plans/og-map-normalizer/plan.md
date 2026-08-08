# Plan: OG Map Normalizer — Convert OG .map to N64 Colmesh Pipeline

## Context

We have tried twice to convert the OG 1-1 map (`Celeste64-og/Content/Maps/1-1.map`) through our `.map → .lvl → .colmesh` pipeline. Both attempts (`convert-og-1-1-map` and `og-1-1-conversion`) failed to produce a playable colmesh.

### Why the old plans failed

Both old plans followed the same pattern: **modify `bake_map.py` incrementally to handle OG entity classes one-by-one**. This approach hit a wall because:

1. **Entity classnames encode gameplay behavior, not material properties.** OG maps use classnames like `SpikeBlock`, `DeathBlock`, `TrafficBlock` to signal "this geometry kills the player" or "this is a moving platform." Our pipeline reads material properties from **texture name suffixes** (`_death`, `_climbable`, etc.). There is no mapping layer between these two systems.

2. **Geometry from different entity classes overlaps.** SpikeBlock brushes sit on top of worldspawn platforms — they "skin" the same physical space with different gameplay rules. Naively baking both produces double collision with conflicting material flags.

3. **The entity inventory is large and map-specific.** Across all 12 OG maps there are 31 distinct entity classes. Handling each as a special case in `bake_map.py` creates a combinatorial maintenance burden.

4. **The plans never reached the verification stage.** The old Increment 4 ("Full Bake + Colmesh") produced a bare worldspawn shell with zero death surfaces, zero climbable surfaces, and only 2 of 10+ entities — because the filtering approach threw away all non-worldspawn geometry.

### Current pipeline status (verified 2026-07-24)

Running the current pipeline on 1-1.map:
```
[bake] skipped unsupported class=Cassette
[bake] skipped unsupported class=TrafficBlock (×5)
[bake] skipped unsupported class=Node (×5)
[bake] skipped unsupported class=SpikeBlock (×6)
[bake] skipped unsupported class=DeathBlock
[bake] skipped unsupported brush classes: DeathBlock:1, SpikeBlock:6, TrafficBlock:5
face_count=330 vertex_count=1214 collider_count=102 entity_count=2
materials: rock_1, snow_1, rock_2
```

Result: 102 solid faces (worldspawn shell), 228 visual-only faces (decorations), 2 entities (PlayerSpawn + Strawberry). Zero death surfaces. Zero climbable surfaces. Missing: Cassette, all spike geometry, all death volumes, all moving platform geometry.

### What the OG 1-1 map actually contains

| Entity Class | Count | Brushes | Faces | Current Fate |
|-------------|-------|---------|-------|--------------|
| worldspawn | 1 | 13 | 102 | ✅ Baked as solid |
| Decoration | 23 | 23 | 228 | ✅ Baked as visual-only |
| SpikeBlock | 6 | 6 | 36 | ❌ Skipped entirely |
| DeathBlock | 1 | 1 | 6 | ❌ Skipped entirely |
| TrafficBlock | 5 | 5 | 30 | ❌ Skipped entirely |
| PlayerSpawn | 1 | 0 (point) | 0 | ✅ Baked as entity |
| Strawberry | 1 | 0 (point) | 0 | ✅ Baked as entity |
| Cassette | 1 | 0 (point) | 0 | ❌ Skipped (false positive) |
| Node | 5 | 0 (point) | 0 | ❌ Skipped (correct) |
| func_group | 1 | 0 | 0 | ❌ Skipped (correct) |
| StaticProp | 11 | 0 (point) | 0 | ❌ Not handled |

**The gap:** 72 game-relevant faces (36 spike + 6 death + 30 moving platform) are completely lost. That's ~41% of the non-decoration geometry.

## Architectural decisions

- **Decision: Pre-processor normalization tool.** Write a new standalone tool `normalize_og_map.py` that translates OG `.map` conventions to our `.map` conventions. It sits BEFORE `bake_map.py` in the pipeline. Rationale: keeps the existing, working pipeline unchanged; concentrates all OG-specific logic in one place; produces inspectable intermediate output; scales to all 12 OG maps through a single mapping table. The normalized `.map` is written to `build/` (not `/tmp` — volatile), and is an `.INTERMEDIATE` Makefile target so it's regenerated when the source changes.

- **Decision: Entity-class → material-suffix mapping table.** A centralized Python dict maps OG classnames to (our_classname, texture_suffix, face_filter). Rationale: one source of truth for the conversion; easy to extend for new maps; the mapping is data, not code. Unknown classes are explicitly logged with a reason and skipped. The `SKIPPED_CLASSES` dict documents each skip reason for auditability when scaling to other maps (Coin, Feather, SignPost, StaticProp, NPCs, etc.).

- **Decision: Upward-face-only filter for SpikeBlock.** Only emit SpikeBlock faces whose transformed normal points upward (normal.y > 0.3 ≈ cos(72.5°) — steeper than any walkable surface) as death surfaces. Skip downward and sideways faces. The face normal is computed from the Quake face's three defining points: `n = cross(p2-p1, p3-p1)`. If `|n| < 1e-10`, the face is degenerate and skipped. Rationale: SpikeBlock geometry overlaps worldspawn — the spike block is a "death skin" on top of platforms. Emitting all faces would create death surfaces inside solid geometry, killing the player just for standing near a spike block. The upward-only filter preserves the intended gameplay: you die landing ON spikes, not standing next to them.

- **Decision: Complex entities baked as static solid.** TrafficBlock, FallingBlock, FloatyBlock, MovingBlock, GateBlock, BreakBlock, CassetteBlock, DoubleDashPuzzleBlock all become `func_wall` with solid material. Rationale: preserves the level shape and traversal paths; movement behavior requires runtime systems we don't have; static geometry is strictly better than missing geometry.

- **Decision: Texture suffix guard against double-appending.** The `remap_texture()` function checks whether the texture name already ends with the target suffix before appending. `rock_1_death` on a SpikeBlock stays `rock_1_death`, not `rock_1_death_death`. Rationale: prevents suffix stacking when maps already use our naming conventions.

- **Decision: Preserve all `_tb_*` TrenchBroom metadata keys.** The normalizer passes through all `_tb_*` entity properties unchanged so the normalized `.map` remains openable in TrenchBroom for inspection and manual fixup. Rationale: debugging the conversion requires being able to open the output in the same editor.

- **Decision: Asset-only scope.** MAT_DEATH (0x0004) is defined in `coll_mesh.hpp` but the runtime has no code that checks it. This plan bakes death flags into the colmesh but does NOT wire up runtime death response. That is separate follow-up work. Rationale: keeps this plan focused on the asset conversion problem; the runtime change is trivial (check `hit.material & MAT_DEATH` in the motor) but needs its own testing.

- **Decision: Rejected — modifying bake_map.py directly.** The old approach of adding `DEATH_BRUSH_CLASSES` sets and special-case emit paths inside `bake_map.py` was rejected because: (a) it makes `bake_map.py` responsible for OG-specific knowledge that belongs in a conversion layer, (b) it doesn't solve the geometric overlap problem, (c) each new map would require more special cases.

- **Decision: Rejected — CSG subtraction of entity brushes from worldspawn.** Mathematically correct but far too complex for the current toolchain. The upward-face filter is a pragmatic approximation that works for the actual geometry patterns in OG maps.

- **Decision: Rejected — `--profile` CLI argument for per-map overrides.** The initial draft included a `--profile 1-1` argument but no profile mechanism was defined. For the 1-1-only scope, there is exactly one mapping table and no per-map variation. If later maps need different rules, profiles can be added as a mapping-table override file, not a CLI flag. Removed from the plan.

## Assumptions and answers from code

- **Assumption: The coordinate transform in the normalizer matches bake_map.py.** Source: `tools/bake_map.py:transform_normal` — maps Quake-space normals `(nx, ny, nz)` to game-space `(nx, nz, -ny)` (rotation only, scale-free). The normalizer imports only `transform_normal` — it does NOT need `transform_point` (which couples to `WORLD_SCALE`, and 1-1 uses 0.15 not the default 0.2). The upward-face filter uses `transform_normal(n).y` which equals the original Quake Z (up) component — faces with `nz > 0.3` point upward.

- **Assumption: colmesh_bake.py's material_flags() handles `_death` suffix.** Source: `tools/colmesh_bake.py:material_flags` — `if t.endswith("_death") or t.endswith("_kill"): flags |= MAT_DEATH`. Confirmed.

- **Assumption: The existing LVL format and level_loader don't need changes.** Source: `tools/lvl_format.py` — Face flags already support solid (0x01) and visual-only (0x02). Death/climbable/ice are encoded in colmesh material flags, not LVL face flags. Confirmed.

- **Answer from code: 31 entity classes across all OG maps.** Source: regex inventory of `assets/og_converted/maps/*.map` — `Decoration:447, SpikeBlock:445, StaticProp:206, FallingBlock:145, FloatingDecoration:67, Node:37, func_group:32, Strawberry:30, Refill:28, CassetteBlock:24, PlayerSpawn:22, Coin:22, Feather:22, FloatyBlock:22, Cassette:20, TrafficBlock:20, DeathBlock:17, BreakBlock:16, worldspawn:12, Spring:10, GateBlock:7, MovingBlock:5, SignPost:5, IntroCar:2, Granny:1, Theo:1, Badeline:1, Chimney:1, DoubleDashPuzzleBlock:1, FixedCamera:1, EndingArea:1`.

- **Answer from code: TB_empty texture has no corresponding sprite.** Source: `filesystem/tex/` contains `TB_empty.sprite` — a sprite exists in the Makefile DFS_TEX_FILES list. But the texture name `TB_empty` won't match any material flag suffix. DeathBlock uses `TB_empty` — the normalizer must remap this to `TB_empty_death` so colmesh_bake.py picks up the death flag.

- **Answer from code: Cassette entity_id = 9 is defined.** Source: `tools/entity_ids.py` — `"Cassette": 9`. The entity is point-only (no brushes). The current `bake_map.py` puts `Cassette` in `UNSUPPORTED_BRUSH_CLASSES`, which causes a log line "skipped unsupported class=Cassette" but does NOT prevent the point entity from being baked — `UNSUPPORTED_BRUSH_CLASSES` only gates the `emit_brush_faces()` loop. The point entity path runs first and correctly emits Cassette at entity_id=9. Verified: the current `filesystem/lvl/1-1.lvl` has `entity_count=3` (PlayerSpawn, Strawberry, Cassette) — the `entity_count=2` in the old plan was incorrect. The "skipped unsupported" log message for Cassette is cosmetic noise, not a functional bug. The normalizer approach makes this moot: the normalized .map uses our supported classnames, so no false-positive log messages appear.

- **Answer from code: PlayerSpawn entity_id = 0, Strawberry = 1, Refill = 2, Spring = 3.** Source: `tools/entity_ids.py`. These already map correctly.

## Risks accepted

- **Risk: Upward-face filter may miss non-horizontal spike surfaces.** Some OG maps may have spike walls (vertical spike surfaces). The Y > 0.3 filter would skip them. Mitigation: for 1-1, all SpikeBlocks are floor spikes — the filter is correct. If future maps need wall spikes, add a configurable filter mode per entity.

- **Risk: Normalized .map may produce different triangle counts than original.** The normalizer re-emits brushes with potentially different texture names but identical geometry. The polygon clipping and triangulation in bake_map.py should produce identical results. Mitigation: compare triangle counts before/after normalization in tests.

- **Risk: TB_empty_death may not have a valid sprite in the DFS.** The runtime material catalog (`material_catalog.cpp:29`) explicitly checks `strcmp(line, "TB_empty") == 0` and sets the sprite to `nullptr`. The normalized map produces `TB_empty_death` instead, which won't match this check. It falls through to the probe path: `sprite_load("rom:/tex/TB_empty_death.sprite")`, which fails (file doesn't exist) and returns `nullptr`. Then `level_renderer.cpp:136` skips null materials. Confirmed safe — both code paths result in `nullptr` sprite, and death surfaces are invisible by design.

- **Risk: Static solid bake of TrafficBlock geometry may create unintended platforms.** TrafficBlocks in OG are moving platforms that carry the player. Baking them as static solid creates permanent floors/walls that may block intended paths. Mitigation: for 1-1, the TrafficBlocks are small platforms within the main path — baking them as static is acceptable. If this causes issues in other maps, we can skip specific entity classes per-map.

## Increment DAG

- Inc 1 — Normalizer tool + mapping table (M) — depends on: none — unblocks: 2, 3
- Inc 2 — Pipeline integration + 1-1 full bake (M) — depends on: 1 — unblocks: 4
- Inc 3 — Validation & smoke tests (M) — depends on: 2 — unblocks: none
- Inc 4 — Makefile & DFS integration (S) — depends on: 2 — unblocks: none

Inc 3 and Inc 4 can run in parallel after Inc 2. **Note:** Inc 4's Makefile rule depends on the normalizer output path (`build/1-1-norm.map`). If Inc 3 changes this path during testing, Inc 4 must be updated. This is a soft coupling — coordinate the path before starting either increment.

## Increments

### Inc 1 — Normalizer tool + entity mapping table (M)
**Depends on:** none
**Unblocks:** 2, 3
**Done criteria:** `normalize_og_map.py` converts `1-1.map` to a normalized `.map` where SpikeBlock faces have `_death` suffix, DeathBlock faces have `_death` suffix, TrafficBlock becomes func_wall, and point entities are preserved.

#### Files to touch

##### tools/normalize_og_map.py (NEW)
- What changes: New standalone Python tool. Reads OG Quake `.map`, applies entity class mapping, remaps textures, filters faces, emits normalized `.map`.
- Function(s):
  - `parse_map_file(path) -> list[dict]` — imported from `bake_map.py` (reuse, not reimplement)
  - `transform_normal(n: Vec3) -> Vec3` — imported from `bake_map.py` (rotation-only, scale-free; only this transform is needed, NOT `transform_point`)
  - `compute_face_normal(p1, p2, p3) -> Vec3 | None` — `n = cross(p2-p1, p3-p1)`; if `|n| < 1e-10`, return None (degenerate face → skip)
  - `normalize_entity(entity, mapping) -> dict` — apply class mapping: replace classname, remap texture per-face, filter faces by rule
  - `normalize_brush_faces(brush, classname, rules) -> list[FaceDef]` — for each face: compute normal (if degenerate, skip); if rule=="upward_only" and `transform_normal(n).y <= 0.3`, skip; append texture suffix; return filtered face list
  - `remap_texture(tex_name: str, suffix: str) -> str` — if suffix is non-empty and `tex_name` doesn't already end with suffix, append suffix. Guard: `if suffix and not tex_name.endswith(suffix): return tex_name + suffix`. Handles `TB_empty` → `TB_empty_death` for DeathBlock.
  - `emit_map_file(entities: list[dict], path: str)` — write normalized `.map` in Standard Quake format: one brace per line, no indentation (matching the input convention), texture parameters with 6 decimal places, entities separated by a blank line, no block comments emitted, `// brush N` comments preserved, all `_tb_*` keys passed through unchanged. First line is a provenance comment: `// normalized from: <source_path>`.
  - `main()` — CLI: `python3 tools/normalize_og_map.py <in.map> <out.map>`
  - Note: `--profile` was considered but rejected — for 1-1-only scope, there is exactly one mapping table with no per-map variation. If later maps need different rules, add a mapping-override JSON file, not a CLI flag.
- Data shapes:
  ```python
  ENTITY_MAPPING = {
      # (our_classname, texture_suffix, face_filter)
      "worldspawn":        ("worldspawn",  "",        None),
      "PlayerSpawn":       ("PlayerSpawn", "",        None),
      "Strawberry":        ("Strawberry",  "",        None),
      "Refill":            ("Refill",      "",        None),
      "Spring":            ("Spring",      "",        None),
      "Cassette":          ("Cassette",    "",        None),
      "SpikeBlock":        ("func_wall",   "_death",  "upward_only"),
      "DeathBlock":        ("func_wall",   "_death",  None),
      "Decoration":        ("Decoration",  "",        None),
      "FloatingDecoration":("Decoration",  "",        None),
      "TrafficBlock":      ("func_wall",   "",        None),  # static solid
      "FallingBlock":      ("func_wall",   "",        None),
      "FloatyBlock":       ("func_wall",   "",        None),
      "GateBlock":         ("func_wall",   "",        None),
      "MovingBlock":       ("func_wall",   "",        None),
      "CassetteBlock":     ("func_wall",   "",        None),
      "BreakBlock":        ("func_wall",   "",        None),
      "DoubleDashPuzzleBlock": ("func_wall", "",      None),
  }

  SKIPPED_CLASSES = {
      # classname → reason (audit trail for scale-out to other maps)
      "Node":           "pathfinding node, no geometry",
      "func_group":     "TrenchBroom layer container, no geometry",
      "StaticProp":     "visual-only point entity, no collision",
      "Coin":           "needs coin runtime (future)",
      "Feather":        "needs feather runtime (future)",
      "SignPost":       "needs sign/dialog runtime (future)",
      "IntroCar":       "cutscene entity, no gameplay collision",
      "Granny":         "NPC, no collision geometry",
      "Theo":           "NPC, no collision geometry",
      "Badeline":       "NPC, no collision geometry",
      "Chimney":        "needs chimney runtime (future)",
      "FixedCamera":    "camera hint, no geometry",
      "EndingArea":     "trigger volume, no geometry",
  }
  ```
- Integration points: Imports `parse_map_file` and `transform_normal` from `bake_map.py` to guarantee coordinate transform consistency. Does NOT import `transform_point` (unused, and its coupling to `WORLD_SCALE` default 0.2 vs 1-1's 0.15 is a latent trap).
- Error paths: Unknown entity class → log warning and skip (use `SKIPPED_CLASSES` reason if known, else "unrecognized"). Brush with <4 faces → skip. Face normal computation returns None (degenerate 3 collinear points) → skip face. Face normal filter removes all faces → warn that entity produced no geometry.

##### tools/bake_map.py
- What changes: Ensure `parse_map_file` and `transform_normal` are cleanly importable by `normalize_og_map.py`. These are already module-level functions; verify they don't have side effects on import. Add `if __name__ == "__main__"` guard if not already present.
- Function(s): No new functions — just verify importability.
- Integration points: Used by `normalize_og_map.py`.
- Note: The old plan proposed removing `Cassette` from `UNSUPPORTED_BRUSH_CLASSES`, but this is unnecessary — `UNSUPPORTED_BRUSH_CLASSES` only gates `emit_brush_faces()`, not point entity baking. Cassette is already correctly baked as entity_id=9. With the normalizer, Cassette is mapped to our supported `Cassette` classname, so the log noise disappears naturally.

#### Edge cases
- TB_empty texture on DeathBlock: remap to `TB_empty_death` so colmesh_bake picks up death flag
- Brushes with mixed textures: each face keeps its own texture; suffix is appended per-face
- Non-axis-aligned SpikeBlock faces: normal check uses transformed (game-space) normal
- func_group entities: they have no brushes in OG maps, just skip silently
- StaticProp entities: point entities with no origin property in some cases — skip if no origin

#### Verification
- Run: `python3 tools/normalize_og_map.py assets/og_converted/maps/1-1.map build/1-1-normalized.map`
- Check: `grep -c '_death' build/1-1-normalized.map` — expect 42 (36 spike faces + 6 death faces)
- Check: `grep -c '"classname" "func_wall"' build/1-1-normalized.map` — expect 12 (6 SpikeBlock + 1 DeathBlock + 5 TrafficBlock)
- Check: `grep -c 'PlayerSpawn' build/1-1-normalized.map` — expect 1
- Check: `grep -c 'Strawberry' build/1-1-normalized.map` — expect 1
- Check: `grep -c 'Cassette' build/1-1-normalized.map` — expect 1
- Check: `grep 'TB_empty' build/1-1-normalized.map` — expect `TB_empty_death` (not bare `TB_empty`)

### Inc 2 — Pipeline integration + 1-1 full bake (M)
**Depends on:** Inc 1
**Unblocks:** 3, 4
**Done criteria:** Running the full pipeline (normalize → bake → colmesh) on 1-1 produces a colmesh with solid, death, and visual-only triangles. No entity classes are skipped except intentional ones (Node, func_group, StaticProp).

#### Files to touch

##### tools/normalize_og_map.py
- What changes: Any bug fixes found during integration testing.

##### tools/bake_map.py
- What changes: No changes required. The normalized .map uses our supported entity classes and texture suffixes — it feeds directly into `bake_map()` with no modifications needed. The old plan's proposed Cassette removal from `UNSUPPORTED_BRUSH_CLASSES` is unnecessary (see Inc 1 analysis — the point entity path already handles Cassette correctly).
- Function(s): No changes.
- Integration points: The normalized .map feeds directly into `bake_map()`.

##### tools/colmesh_bake.py
- What changes: Verify it handles `_death` suffix correctly (already does). No changes expected.
- Function(s): `material_flags()` — already correct.

##### tests/fixtures/1-1-normalized.manifest (NEW)
- What changes: Expected manifest for the normalized 1-1 bake. Used by smoke tests. The existing `tests/fixtures/1-1.manifest` (old 3-material manifest: `rock_1, snow_1, rock_2`) is **replaced in-place** — the normalized bake becomes the canonical 1-1, so the old bare-worldspawn manifest is obsolete. Rename the old one to `tests/fixtures/1-1-legacy.manifest` as an archival reference.
- Data shapes: List of material names, one per line. Expected entries include `rock_1`, `snow_1`, `rock_2`, `floor_dirty_concrete_death`, `TB_empty_death`, `metal_floor_1`.

#### Edge cases
- The normalized .map may have material names not in the sprite catalog (e.g., `TB_empty_death`). This is OK — the colmesh doesn't need sprites, and the material catalog can handle unknown materials gracefully.
- The normalized .map must preserve the original `_tb_textures` and `_tb_def` worldspawn keys so TrenchBroom can still open it for inspection.

#### Verification
- Run: `python3 tools/normalize_og_map.py assets/og_converted/maps/1-1.map build/1-1-norm.map`
- Run: `python3 tools/bake_map.py build/1-1-norm.map build/1-1-norm.lvl build/1-1-norm.manifest --world-scale 0.15`
- Run: `python3 tools/colmesh_bake.py build/1-1-norm.lvl build/1-1-norm.colmesh`
- Run: `python3 tools/level_bake_report.py build/1-1-norm.map build/1-1-norm.lvl`
- Check: `duplicate_vertex_faces=0`, `first_fan_degenerate_faces=0`, `reversed_winding_faces=0`
- Check: colmesh contains triangles with `MAT_DEATH` flag (hex dump or Python inspection)
- Check: entity_count ≥ 4 (PlayerSpawn + Strawberry + Cassette + Refill if present)

### Inc 3 — Validation & smoke tests (M)
**Depends on:** Inc 2
**Unblocks:** none
**Done criteria:** All existing smoke tests pass. New tests verify death triangle presence, entity counts, and material flag correctness in the baked colmesh.

#### Files to touch

##### tests/bake_map_smoke.py
- What changes: Add two test cases for the normalization pipeline.
- Function(s):
  - `test_normalize_og_1_1()`: Run normalizer on `assets/og_converted/maps/1-1.map`. Assert: output file exists. Assert: at least 36 lines contain `_death` (6 SpikeBlock brushes × 6 faces each). Assert: at least 5 occurrences of `"classname" "func_wall"` (6 SpikeBlock + 1 DeathBlock + 5 TrafficBlock = 12 entities reclassified to func_wall; but some entities may share the same brush count, so assert ≥ 5 as lower bound). Assert: no `UNSUPPORTED_BRUSH_CLASSES` classnames appear in the output (SpikeBlock, DeathBlock, TrafficBlock, etc. are all remapped).
  - `test_normalized_bake_matches_manifest()`: Bake the normalized 1-1.map through `bake_map.py`. Assert: manifest contains `TB_empty_death` (from DeathBlock). Assert: manifest contains `floor_dirty_concrete_death` (from SpikeBlock). Assert: entity_count ≥ 4 (PlayerSpawn id=0, Strawberry id=1, Cassette id=9, Refill id=2 if present).
- Data shapes: String assertions on manifest file contents and baked LVL entity table.
- Integration points: Shells out to `normalize_og_map.py` and `bake_map.py` as subprocesses (avoids import coupling).

##### tests/colmesh_smoke.py (NEW if not existing, else UPDATE)
- What changes: Add colmesh validation for the normalized 1-1 bake.
- Function(s):
  - `test_colmesh_has_death_triangles()`: Parse `1-1.colmesh` binary (big-endian). Read header → triangle_count, triangle_offset. Seek to triangle_offset. For each triangle (12 bytes: 3×uint16 indices + uint16 material + uint16 face_id + uint16 pad), check `(material & 0x0004) != 0`. Assert at least one triangle has MAT_DEATH flag.
  - `test_colmesh_triangle_count()`: Assert total triangle count is between 200 and 400 (expected range for 1-1: 102 solid + ~42 death + ~30 trafficblock solid = ~200-350 after triangulation). Assert triangle_count ≤ 32767 (max for uint16 BVH indexing).
- Data shapes: Big-endian binary struct unpacking per `docs/colmesh_format.md`.
- Integration points: Uses `struct.unpack(">HHHHHH", ...)` for triangle records.

##### tools/level_bake_report.py
- What changes: Add material flag summary to report output. Since `level_bake_report.py` reads `.lvl` files (which only encode solid=0x01 and visual=0x02 in face flags), the death/climbable/ice counts are derived from manifest texture names. Import `material_flags()` from `colmesh_bake.py` (or factor it into a shared `material_utils.py`). For each LVL face, look up the material name via its material_id → manifest index, apply `material_flags()`, and accumulate counts per flag bit.
- Function(s): `summarize()` — add material breakdown section, computing flags per-face from the manifest.
- Data shapes: New output line: `material_flags=solid:N death:N climbable:N ice:N visual:N`
- Integration points: Imports `material_flags` from `tools/colmesh_bake.py` (or a new shared `tools/material_utils.py` if circular imports are a concern — `colmesh_bake.py` already imports `lvl_format`, so `level_bake_report.py` importing back from `colmesh_bake.py` is a one-way dependency, not circular).

#### Edge cases
- If the normalized .map has no death geometry (e.g., all SpikeBlocks filtered out by the upward-face rule), the test should warn but not fail — some maps may legitimately have no death surfaces.
- The colmesh binary parser in tests must handle big-endian correctly.

#### Verification
- Run: `python3 tests/bake_map_smoke.py`
- Run: `python3 tests/colmesh_smoke.py`
- Run: `python3 tests/level_bake_report_smoke.py`
- All pass with zero errors.

### Inc 4 — Makefile & DFS integration (S)
**Depends on:** Inc 2
**Unblocks:** none
**Done criteria:** `make` builds `1-1.colmesh` through the normalization pipeline. The ROM DFS includes the new colmesh.

#### Files to touch

##### Makefile
- What changes: Update the 1-1 bake rule to include the normalization step. The normalized `.map` is written to `build/1-1-norm.map` (not `/tmp` — volatile) and declared `.INTERMEDIATE` so `make` knows it's a derived artifact that can be deleted.
- Function(s): None (build system change)
- Integration points:
  ```makefile
  .INTERMEDIATE: build/1-1-norm.map

  build/1-1-norm.map: assets/og_converted/maps/1-1.map tools/normalize_og_map.py
      @mkdir -p $(dir $@)
      python3 tools/normalize_og_map.py $< $@

  filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest: \
      build/1-1-norm.map \
      tools/bake_map.py \
      tools/lvl_format.py \
      tools/entity_ids.py | filesystem/lvl
      python3 tools/bake_map.py $< filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest --world-scale 0.15
  ```
  Note: `mkdir -p $(dir $@)` in the recipe avoids defining a separate `build` target that would conflict with the existing `build/` directory (which already contains ROM artifacts). No separate `build:` target is needed.

##### filesystem/tex/
- What changes: Verify `TB_empty.sprite` and any other death-surface textures exist. If `TB_empty.sprite` doesn't exist, create a minimal 1x1 transparent sprite (death surfaces are invisible anyway).
- Function(s): None (asset addition)

#### Edge cases
- The normalization step runs every build. `build/1-1-norm.map` is declared `.INTERMEDIATE` — make may delete it after the build. If a subsequent build detects the source `1-1.map` unchanged, it uses the existing `.lvl` and `.colmesh` without re-normalizing. This is correct: normalization is deterministic for a given input.
- Makefile pattern rules: the `1-1.colmesh` pattern rule depends on `1-1.lvl`, which now depends on `build/1-1-norm.map`. The dependency chain is: `1-1.map → build/1-1-norm.map → 1-1.lvl → 1-1.colmesh`. Verify with `make -n` that all steps appear in order.

#### Verification
- Run: `make -n filesystem/lvl/1-1.colmesh` — verify the normalization step appears in the dry-run output
- Run: `./compile-rom.sh` — builds successfully
- Run: `ls -la filesystem/lvl/1-1.colmesh` — file exists and is non-zero

## Cross-cutting verification

After Inc 4, run the full acceptance checklist:

1. **Bake integrity:** `python3 tests/level_bake_report_smoke.py` reports zero degenerate/reversed faces
2. **Death triangles present:** Python inspection of 1-1.colmesh confirms ≥1 triangle with material bit 0x0004 set
3. **Solid triangles present:** Python inspection confirms ≥1 triangle with material bit 0x0001 set
4. **Entity count:** 1-1.lvl contains PlayerSpawn (id=0), Strawberry (id=1), and Cassette (id=9)
5. **ROM builds:** `./compile-rom.sh` produces `madeline_cube_rom.z64`
6. **Budget:** 1-1.colmesh ≤ 256 KB (currently ~5.7 KB — well under)
7. **No skipped geometry:** The normalizer log shows SpikeBlock→func_wall_death, DeathBlock→func_wall_death, TrafficBlock→func_wall (not skipped)

## Standards / common-mistakes referenced

- `.agents/map-creation.md` — applies to: the normalized .map format, material suffix contract
- `.agents/common-mistakes/og-map-polygon-winding.md` — applies to: verifying the normalizer doesn't reintroduce winding bugs
- `docs/colmesh_format.md` — applies to: material flag bit definitions
- `docs/first-room-brief.md` — applies to: material suffix contract, brush-class policy

## Open questions (CONSIDER from review)

- **19 of 31 OG classes handled — scaling audit trail.** The `ENTITY_MAPPING` table covers 19 classes; `SKIPPED_CLASSES` documents 13 more with reasons. When scaling to other maps (1-2 through 1-10, etc.), the 206 `StaticProp` instances and 22 `Coin` instances across all maps may matter. The `SKIPPED_CLASSES` audit trail makes it easy to identify which maps lose what geometry, but the plan doesn't yet include a per-map skip report. Consider adding `--report skip_report.json` to the normalizer for batch auditing.

- **Upward-face threshold of 0.3 is untuned against actual geometry.** The value `0.3 ≈ cos(72.5°)` means surfaces steeper than ~72.5° from horizontal are excluded. This is documented but not empirically verified against 1-1's actual SpikeBlock face normals. Consider dumping all SpikeBlock face normals during Inc 1 verification to confirm the threshold cleanly separates top faces from side faces.

- **`Node` in `UNSUPPORTED_BRUSH_CLASSES` is harmless but asymmetric with the Cassette case.** Both are point entities incorrectly placed in a brush-class set. `Node` being there causes no bug today (Nodes are correctly skipped), but the pattern is fragile — any future point entity accidentally added to `UNSUPPORTED_BRUSH_CLASSES` would get a false-positive skip log. Consider restructuring `bake_map.py` to separate "skip brush processing" sets from "skip point entity" sets, or removing all point-only classes from `UNSUPPORTED_BRUSH_CLASSES`.

- **TB_empty sprite check in Inc 4 may be unnecessary.** The plan says to verify `TB_empty.sprite` exists, but DeathBlock surfaces are invisible by design. The material catalog's null-sprite fallback path (confirmed safe in the Risks section) handles missing sprites gracefully — a missing `TB_empty.sprite` would just mean death surfaces are invisible, which is the desired behavior. Consider dropping the sprite-existence check from Inc 4 and relying on the null-sprite fallback.

- **`_tb_textures`/`_tb_def` paths reference OG project locations.** For 1-1, `_tb_textures` is `"Textures"` and `_tb_def` is `"external:Celeste64.fgd"`. The normalizer preserves these keys, but when opening the normalized `.map` in TrenchBroom for inspection, these paths won't resolve against our project. This is harmless for the pipeline (bake_map.py ignores them) but worth noting in documentation for any manual TrenchBroom workflow.

## Out of scope

- Runtime death-surface handling (checking MAT_DEATH in player motor)
- Converting maps beyond 1-1 (1-2 through 1-10, 1.map, Palette.map)
- TrafficBlock movement behavior (moving platforms)
- BreakBlock dash-break behavior
- Coin/Feather/SignPost runtime support
- NPC entities (Granny, Theo, Badeline)
- Visual mesh (.t3dm) generation from OG brush geometry
- The `first-room` replacement (already works; not part of OG conversion)
