# Room Artifact Contract

This project treats gameplay data and visible static geometry as two related
artifacts with different jobs:

```txt
rom:/lvl/<room>.lvl    gameplay artifact: LVL2 collision, entities, metadata
rom:/lvl/<room>.t3dm   render artifact (offline at this stage): static visible
                       room geometry + materials
rom:/lvl/<room>.colmesh collision mesh: BVH over quantized triangles
rom:/lvl/<room>.nav     offline sidecar: TrafficBlock path data (no runtime
                       consumer yet)
```

## LVL2 (current level format)

The shipping gameplay artifact is **LVL2**, serialized by
`tools/lvl_format.py` and consumed by `src/user/gameplay/world/level_loader.cpp`.
The header is 0x44 (68) bytes and carries counts for colliders, faces, vertices,
entities and strings, plus atmosphere fields (skybox/music/ambience/snow) and
byte offsets to each section. Full layout:

```txt
+0x00  magic            char[4]  "LVL2"
+0x04  version          uint32   = 2
+0x08  collider_count   uint32
+0x0C  face_count       uint32
+0x10  vertex_count     uint32
+0x14  entity_count     uint32
+0x18  string_count     uint32
+0x1C  skybox_str_id    uint16
+0x1E  music_str_id     uint16
+0x20  ambience_str_id  uint16
+0x22  snow_amount_q8   uint16
+0x24  snow_dir_x/y/z   int16[3]
+0x2A  reserved         uint16
+0x2C  off_strings      uint32
+0x30  off_colliders    uint32
+0x34  off_faces        uint32
+0x38  off_vertices     uint32
+0x3C  off_entities     uint32
+0x40  off_props_blob   uint32
```

Face flags (uint16): bit 0 = solid, bit 1 = visual_only.

The `.lvl` is the gameplay + visible-geometry source the runtime renders today
(`LvlRoomRenderer`). The `.colmesh` is the collision source, baked from the same
`ParsedMap` in `tools/writers/colmesh_writer.py`.

## T3DM is an offline artifact at this stage

The `.t3dm` is produced and validated by the offline pipeline (GLB intermediary
→ `gltf_to_t3d` → `tools/patch_t3dm_materials.py`) but is **not connected to
the runtime yet**:

- `GameplayScene` still loads `LvlRoomRenderer` from the `.lvl`.
- The renderer cutover to T3DM is explicitly out of scope for the current
  pipeline migration.
- T3DM remains chunk-based (`T3M` magic + version byte + chunk pointer table);
  the structural summary is recorded in the baseline for later parity checks.

## Baseline

The versioned reference for `1-1` lives in `tests/fixtures/baseline/`:

- `baseline.json` — map SHA-256, scale, format versions and artifact counts.
- `1-1/` — decoded summaries of LVL2, colmesh, NAV and the current T3DM, plus
  the manifest and the T3DM bytes.

Regenerate with `python3 tools/bake_baseline.py`. The baseline is frozen
reference data; parity tests compare against it rather than capturing
expectations at runtime.

## Brush-class policy

Every brush-bearing source class must declare both axes before it may emit data:

```txt
render_mode: static_mesh | actor_model | none | unsupported
collision_mode: solid | actor_owned | trigger | none | unsupported
```

The first-room audit is expected to resolve the classes currently present in the
OG map, including at least `worldspawn`, `Decoration`, `SpikeBlock`,
`TrafficBlock`, and `DeathBlock`. No class may silently become visible or solid
just because it happens to carry brushes.

## Material policy

The legacy LVL1 render path may continue using manifest-based validation while it
exists. The active `.t3dm` render path must validate its own material references
before ROM bundling so TMEM safety follows the artifact that actually renders.
