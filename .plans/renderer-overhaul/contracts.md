# Contracts

## Build-time room contract

For each room id `<room>` the build must produce:

```txt
rom:/lvl/<room>.lvl    gameplay artifact: collision, entities, metadata
rom:/lvl/<room>.t3dm   render artifact: static visible room geometry + material references
```

Host validation must reject artifacts when:

- a material exceeds the chosen TMEM policy
- a render face is non-finite, degenerate, non-triangulated, or has inverted winding
- an included source brush class has no declared `render_mode` or `collision_mode`
- source counts and emitted counts disagree without an explicit rejection reason

## LVL migration contract

- This plan keeps LVL1 on disk. `tools/lvl_format.py` and existing LVL1 files remain readable.
- After cutover, LVL1 face/vertex sections are legacy compatibility payload only; shipping static-room visuals come from `.t3dm`.
- Removing LVL1 render payload or introducing LVL2 is explicitly out of scope for this plan and requires a later schema migration plan.

## Brush-class policy contract

Every brush-bearing class must declare both axes:

```txt
render_mode: static_mesh | actor_model | none | unsupported
collision_mode: solid | actor_owned | trigger | none | unsupported
```

No brush class may be emitted into a render artifact or collision artifact by default inference alone.

## Render-material validation contract

- Manifest-based TMEM validation remains valid only for the legacy `.lvl` render path.
- The new `.t3dm` path must have its own deterministic validator that reads render-artifact material references and verifies every referenced sprite against the chosen TMEM policy before ROM bundling.

## Runtime room contract

```cpp
LoadLevelGameplay(room_id) -> Room
LoadLevelRenderAsset(room_id) -> StaticRoomModel
```

- Gameplay load failure may fall back to the graybox/test room only in debug builds.
- Render load failure must log the missing artifact and avoid drawing stale geometry.
- Static room rendering uses tiny3d model/material APIs rather than direct per-face sprite upload loops.

## Debug contract

The overhaul must leave behind three cheap diagnostics:

1. a room-scale known-good tiny3d fixture with an expected screenshot or checksum that proves the room path independently of the map pipeline
2. a deterministic bake report for a room that states source brush classes, emitted render geometry, rejected geometry, material set, validation failures, and an explicit route decision: `map-derived render mesh viable` or `re-author first room`
3. a host-side debug export (for example OBJ) so malformed bake output can be inspected outside Ares
