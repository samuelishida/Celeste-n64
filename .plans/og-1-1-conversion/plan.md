# Plan: OG 1-1 Map Conversion for Colmesh Pipeline

## Context

We have a working `.map` → `.lvl` → `.colmesh` pipeline. The current `bake_map.py` already handles `worldspawn`, `func_wall`, `func_climbable`, and `Decoration` brushes. However, the OG 1-1 map contains entity types that are either skipped entirely or not properly converted for collision:

**Current 1-1 entity inventory:**
| Entity | Count | Current handling | Desired handling |
|--------|-------|-----------------|------------------|
| `worldspawn` | 1 (12 brushes) | ✅ Baked as solid | Keep as-is |
| `PlayerSpawn` | 1 | ✅ Baked as entity | Keep as-is |
| `Cassette` | 1 | ⚠️ Skipped (UNSUPPORTED_BRUSH_CLASSES) | Bake as entity, no collision needed |
| `Strawberry` | 1 | ✅ Baked as entity | Keep as-is |
| `TrafficBlock` | 5 | ❌ Skipped (UNSUPPORTED_BRUSH_CLASSES) | Skip for now, too complex |
| `Node` | 5 | ❌ Skipped (UNSUPPORTED_BRUSH_CLASSES) | Skip for now |
| `SpikeBlock` | 6 | ❌ Skipped (UNSUPPORTED_BRUSH_CLASSES) | **Convert to death material** |
| `Decoration` | 23 | ✅ Baked as visual-only | Keep as-is |
| `StaticProp` | 11 | N/A (point entities, no brushes) | Skip for now |
| `func_group` | 1 | N/A (Trenchbroom layer container) | Ignore silently |
| `DeathBlock` | 1 | ❌ Skipped (UNSUPPORTED_BRUSH_CLASSES) | **Convert to death material** |

## Goal

Make the OG 1-1 map produce a playable colmesh where:
1. SpikeBlock brushes become death-surface collision triangles
2. DeathBlock brushes become death-surface collision triangles
3. Cassette is baked as an entity (not skipped with error)
4. A conversion report is generated showing what was kept, converted, or dropped

## Changes

### 1. `tools/bake_map.py` — classify SpikeBlock & DeathBlock as death geometry

**Problem:** Both are in `UNSUPPORTED_BRUSH_CLASSES`, so their brushes are logged and skipped entirely. This means the spike hazards and kill volume from 1-1 don't appear in the baked level at all.

**Fix:** Add a new classification set `DEATH_BRUSH_CLASSES` and a new bake path for death geometry.

```python
DEATH_BRUSH_CLASSES = {
    "SpikeBlock",
    "DeathBlock",
}
```

In `bake_map()`, add a new call to `emit_brush_faces()` for death classes. The key difference from solid geometry:
- `face_flags` should still be `0x01` (solid) in the LVL face — the death behavior comes from the material name in colmesh bake
- The texture name needs to carry `_death` suffix so `colmesh_bake.py` maps it to `MAT_DEATH`

Since SpikeBlock and DeathBlock already use their own texture names (`floor_dirty_concrete` for spikes), we need to ensure the material name gets remapped with a `_death` suffix during colmesh bake. Two approaches:

**Approach A (simpler):** In `bake_map.py`, when emitting death brush faces, override the texture name to append `_death` suffix before interning. This way `colmesh_bake.py`'s existing `material_flags()` function will pick it up.

**Approach B (cleaner):** Add a new `face_flags` bit for death in LVL format and have `colmesh_bake.py` check that flag instead of texture suffix.

**Decision:** Approach A is simpler and doesn't require LVL format changes. The texture name is already interned and remapped, so we just need to ensure the manifest entry ends with `_death`.

Changes in `bake_map.py`:
- Add `DEATH_BRUSH_CLASSES` set
- Remove `SpikeBlock` and `DeathBlock` from `UNSUPPORTED_BRUSH_CLASSES`
- Add `emit_brush_faces(DEATH_BRUSH_CLASSES, 0x01, True)` call, but with a texture override: when processing death brushes, append `_death` to the texture name before interning
- Track death brush counts separately in the skipped summary

### 2. `tools/bake_map.py` — handle Cassette as entity, not unsupported

**Problem:** `Cassette` is in `UNSUPPORTED_BRUSH_CLASSES`, but it's a point entity (no brushes). The skip logic only applies to brush-bearing classes, so this is actually a false positive — the log message says "skipped unsupported class=Cassette" even though there's nothing to skip.

**Fix:** Remove `Cassette` from `UNSUPPORTED_BRUSH_CLASSES`. It's already handled as a point entity by the existing entity baking code (entity_id = 9 for Cassette). The `level_loader.cpp` already handles `kEntCassette` and sets `room.cassette`.

Actually, looking more carefully: `Cassette` appears in `UNSUPPORTED_BRUSH_CLASSES` which is only checked for brush-bearing entities. Point entities go through the entity baking path. So removing it from the unsupported set is sufficient — it will be baked as an entity normally.

### 3. `tools/bake_map.py` — silently ignore `func_group`

**Problem:** `func_group` is a Trenchbroom layer container with no gameplay meaning. It currently has no brushes and no recognized classname, so it's silently ignored by the point entity path (no origin, unknown classname_id). This is fine as-is.

**No change needed.** Just documenting current behavior.

### 4. `tools/colmesh_bake.py` — verify death material mapping works

**Problem:** Need to ensure that texture names ending in `_death` produce the correct material flags.

**Current code already handles this:**
```python
if t.endswith("_death") or t.endswith("_kill"):
    flags |= MAT_DEATH
```

Since we're appending `_death` to the texture name in `bake_map.py`, this will work automatically. Just need to verify the manifest entry carries the suffix.

### 5. New tool: `tools/og_conversion_report.py`

**Purpose:** Generate a human-readable report when converting an OG map, showing:
- Total entity count and breakdown by type
- Which entities were baked (solid, visual, death, entity-only)
- Which entities were skipped and why
- Triangle count and colmesh size estimate
- Budget compliance check

**Input:** Same `.map` file as `bake_map.py`
**Output:** JSON report + console summary

This runs alongside the bake pipeline and provides visibility into what was converted.

### 6. Update Makefile if needed

The current Makefile already bakes `1-1.map` → `1-1.lvl` → `1-1.colmesh`. No changes needed to the build pipeline itself, but we should verify the world scale is correct (currently `0.15` for 1-1 vs `0.2` default).

### 7. Update tests

Add a test case to `tests/bake_map_smoke.py` that verifies:
- SpikeBlock brushes produce faces with `_death` material suffix
- DeathBlock brushes produce faces with `_death` material suffix
- Cassette entity is present in baked LVL
- Conversion report JSON is valid

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Death material triangles block movement incorrectly | Player can't walk on death surfaces | `MAT_DEATH` still has `MAT_SOLID`, so collision works; motor needs to handle death response |
| SpikeBlock geometry overlaps with worldspawn | Double collision, visual artifacts | Spikes are typically placed adjacent to platforms, not overlapping |
| TrafficBlock skip leaves gaps in geometry | Player falls through missing platforms | 1-1 TrafficBlocks are moving platforms; static geometry should cover the base paths |
| Colmesh budget exceeded | ROM size blowup | 256 KB budget check in bake tool; 1-1 is small enough |

## Incremental delivery

**Increment 1:** SpikeBlock + DeathBlock → death material (bake_map.py changes)
**Increment 2:** Cassette entity handling + func_group silence  
**Increment 3:** Conversion report tool
**Increment 4:** Test updates + validation

Each increment should rebuild `1-1.colmesh` and run the smoke tests.

## Assumptions

1. The motor already handles `MAT_DEATH` material flags (kills player on contact) — need to verify this
2. TrafficBlock geometry is not needed for the first playable version (static worldspawn covers the base paths)
3. StaticProp entities can be skipped for now (visual-only, no collision impact)
4. Node entities are only needed for TrafficBlock targeting

## Verification steps

1. Run `bake_map.py` on 1-1.map and check console output for death brush counts
2. Run `colmesh_bake.py` on the resulting .lvl and verify death triangles exist
3. Run `tests/colmesh_smoke.py` to validate binary structure
4. Run `tests/coll_mesh_query_test.cpp` against 1-1.colmesh if it loads that file
5. Check colmesh size is under 256 KB budget
