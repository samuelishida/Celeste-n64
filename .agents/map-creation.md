# Map Creation Guide

End-to-end process for authoring, baking, and loading a new room into the demake engine.

---

## Pipeline

```
.map (Quake/TrenchBroom) ──→ bake_map.py ──→ .lvl  ──→ level_loader.cpp
                                          └──→ .manifest ──→ material_catalog.cpp
                          auto: colmesh_bake.py ──→ .colmesh ──→ LoadCollMesh()
```

All three output files are required. The `.colmesh` is generated automatically by the Makefile pattern rule when `.lvl` is built. Without `.colmesh`, collision falls back to empty colliders and the player falls through the floor.

---

## Step-by-Step: New Room

### 1. Author the map

Create `assets/rooms/<name>/<name>.map` in TrenchBroom. Required:
- One `PlayerSpawn` entity (sets `room.player_start`)
- Worldspawn brushes with texture names matching `.sprite` files in `assets/og_converted/textures/`

### 2. Add Makefile rules

```makefile
# In DFS_LVL_FILES:
DFS_LVL_FILES += \
    filesystem/lvl/<name>.lvl \
    filesystem/lvl/<name>.manifest \
    filesystem/lvl/<name>.colmesh

# Bake rule:
filesystem/lvl/<name>.lvl filesystem/lvl/<name>.manifest: \
    assets/rooms/<name>/<name>.map | filesystem/lvl
	python3 tools/bake_map.py $< filesystem/lvl/<name>.lvl filesystem/lvl/<name>.manifest
```

The pattern rule `filesystem/lvl/%.colmesh: filesystem/lvl/%.lvl` picks up `.colmesh` automatically.

### 3. Add textures to DFS

For each texture used by the map, add to `DFS_TEX_FILES`:
```makefile
DFS_TEX_FILES += filesystem/tex/<name>.sprite
```

The pattern rule `filesystem/tex/%.sprite: assets/og_converted/textures/%.sprite` handles the copy. The `.sprite` file must exist at `assets/og_converted/textures/<name>.sprite`.

### 4. Verify bake

```bash
python3 tools/bake_map.py assets/rooms/<name>/<name>.map /tmp/<name>.lvl /tmp/<name>.manifest
python3 tests/level_bake_report_smoke.py   # update paths in the script first
```

Check output for:
- `duplicate_vertex_faces=0`
- `first_fan_degenerate_faces=0`
- `reversed_winding_faces=0`

If these are nonzero, see `.agents/common-mistakes/og-map-polygon-winding.md`.

### 5. Switch default level

In `src/user/gameplay/scene/gameplay_scene.cpp`:
```cpp
static constexpr const char* kBakedLevelPath = "rom:/lvl/<name>.lvl";
```

Also update the material catalog load call nearby to use `"<name>"` instead of the old name.

### 6. Boot ROM and verify

Check console output for:
```
[lvl] loaded rom:/lvl/<name>.lvl: colliders=0 faces=N vertices=M entities=K
[lvl] colmesh stored: ptr=0x80XXXXXX &room.coll_mesh=0x80YYYYYY
```

If `colmesh stored` is missing: the sidecar failed to load. Check:
- `filesystem/lvl/<name>.colmesh` exists on disk
- The `.colmesh` Makefile rule ran (`make filesystem/lvl/<name>.colmesh`)
- Console shows `[lvl] colmesh FAILED` with the attempted path

---

## Coordinate System

Quake ↔ game transform: `(x * 0.2, z * 0.2, -y * 0.2)` (see `tools/bake_map.py:transform_point`).

| Axis | Quake | Game |
|------|-------|------|
| Right | +X | +X |
| Up | +Z | +Y |
| Forward | -Y | +Z |

Player spawn at Quake `(0, 0, 64)` → game `(0.0, 12.8, 0.0)`. Quake Z=64 (height) → game Y=12.8 (on a 32-unit-high platform).

---

## kPosFp — Choosing Fixed-Point Scale

`kPosFp` in `level_renderer.cpp` packs world-space positions into int16 for the RSP. Max game-unit coordinate that fits: `32767 / kPosFp`.

| kPosFp | Max safe coord |
|--------|---------------|
| 128 | ±256 game units |
| 64 | ±512 game units |
| **32** | **±1024 game units** |

Default is `32`. Only increase it if the level is tiny and you need sub-unit precision.

OG 1-1 spans 908.8 game units (forward axis). `kPosFp=32` gives comfortable margin. `kPosFp=64` would overflow.

---

## Entity IDs

Entity IDs are defined in two mirrored places that **must stay in sync**:

- `tools/entity_ids.py` — used by `bake_map.py`
- `src/user/gameplay/world/entity_ids.hpp` — used by loader + dispatch

Current IDs:

| ID | Class | Where decoded |
|----|-------|--------------|
| 0 | PlayerSpawn | `level_loader.cpp` → `room.player_start` |
| 1 | Strawberry | `entity_dispatch.cpp` |
| 2 | Refill | `entity_dispatch.cpp` |
| 3 | Spring | `entity_dispatch.cpp` |
| 9 | Cassette | `level_loader.cpp` → `room.cassette` |

To add a new entity: add to both files with the same integer, then add a decode branch in `level_loader.cpp` or a `ClassnameToPlaceholder` entry in `entity_dispatch.cpp`.

---

## Textures and MaterialCatalog

MaterialCatalog loads material names from the `.manifest` file (one name per line, in bake order). Material index is positional — baker and runtime must agree.

**TB_empty** is an invisible marker material used by kill volumes. Reserve a null slot for it in MaterialCatalog (no sprite loaded, renderer skips the batch). See `material_catalog.cpp`.

Missing `.sprite` files are skipped with a log line. This silently shifts subsequent material indices if null-slot reservation is not in place.

---

## CollMesh Diagnostics

The colmesh pointer (`room.coll_mesh`) lives as the **last field of Room**, inside the heap-allocated `GameplayScene::Impl`. Corruption produces a CPU exception with a garbage pointer in `a1` on the first `SweepSphereMesh` call.

Diagnostic log added to `level_loader.cpp`:
```
[lvl] colmesh stored: ptr=0x???? &room.coll_mesh=0x????
```

And to `gameplay_scene.cpp` (ReloadBakedLevel):
```
[reload] after LoadLevel+Init: baked=1 coll_mesh=0x???? &coll_mesh=0x???? impl=0x???? sizeof_impl=??????
[reload] after matcat: coll_mesh=0x????
[reload] after dispatch: coll_mesh=0x????
[reload] after resolve: coll_mesh=0x????
[reload] before spawn: coll_mesh=0x????
```

If `&room.coll_mesh` differs between LoadLevel and ReloadBakedLevel: struct layout mismatch (ODR violation, include-order issue, or conditional compilation difference between TUs).

If value changes between stages: active write past the end of an array that precedes `coll_mesh` in `Room`.

---

## Capacity Constants

All of these gate early with a log if exceeded — no silent overflow.

| Constant | Default | Location |
|----------|---------|----------|
| `LevelGeometry::kMaxFaces` | 1024 | `level_loader.hpp` |
| `LevelGeometry::kMaxVertices` | 8192 | `level_loader.hpp` |
| `LevelRenderer::kMaxBatches` | 1024 | `level_renderer.hpp` |
| `Room::kMaxSpawns` | 64 | `world.hpp` |

sizeof(GameplayScene::Impl) ≈ 256KB with current constants. Print to confirm after bumping:
```cpp
debugf("[impl] size=%zu\n", sizeof(Impl));  // remove after checking
```

---

## Verification Checklist

After switching to a new level:

- [ ] `./compile-rom.sh` succeeds
- [ ] `[lvl] loaded rom:/lvl/<name>.lvl` line present in console
- [ ] `[lvl] colmesh stored: ptr=0x80...` present (not `FAILED`)
- [ ] `[matcat] loaded N materials` — count matches manifest
- [ ] Player spawns at authored spawn position
- [ ] Player lands on floor (no fall-through)
- [ ] `level_bake_report_smoke.py` passes with zero degenerate/reversed faces
- [ ] `level_loader_test.cpp` passes

---

## Common Mistakes

- `.agents/common-mistakes/og-map-polygon-winding.md` — reversed/degenerate faces from OG Quake maps
- `.agents/common-mistakes/camera-respawn-reset.md` — stale camera boom after respawn

---

## First-Room as Fallback

`first-room` is a small hand-authored level confirmed working at commit `2dd6540`. If a new level crashes, switch `kBakedLevelPath` back to `"rom:/lvl/first-room.lvl"` to isolate whether the crash is data-specific or a runtime bug.
