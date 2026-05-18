# Data model

## Scope

Database persistence is **N/A**. This file locks the generated artifact model instead, because the failure is happening at the boundary between authored data, baked gameplay data, and render-ready data.

## Entities & relationships

```txt
Authored room source
  ├── gameplay source (.map or equivalent)
  │      └── LevelGameplayArtifact (.lvl, LVL1 compatibility shell)
  │             ├── colliders
  │             ├── entities/spawns
  │             └── legacy face/vertex sections retained but not used by shipping render path
  └── render source (.glb or validated map-derived mesh)
         └── LevelRenderArtifact (.t3dm)
                ├── objects / parts
                ├── materials
                └── texture references

Build metadata / audit report
  └── records material set, validation results, and source-to-output decisions
```

## Constraints & indexes

- `.lvl` remains LVL1 during this overhaul and owns gameplay truth: collision planes, bounds, entities, and room metadata.
- LVL1 face/vertex sections remain present for compatibility during this plan, but shipping static-room rendering must not consume them after cutover.
- `.t3dm` owns visible static room geometry only: vertices, indices, objects, materials, and optional BVH.
- A room id resolves exactly one gameplay artifact and one render artifact by convention: `<room>.lvl` + `<room>.t3dm`.
- Every referenced level material must be TMEM-safe before ROM bundling.
- Render meshes must be triangulated, finite, non-degenerate, and use winding consistent with their stored normals.
- Brush-bearing entity classes must declare both `render_mode` and `collision_mode`; no implicit “all brushes render” or “all brushes collide” rule.

## Read/write patterns

1. Build reads authored room sources and writes `.lvl`, `.t3dm`, and audit/build metadata.
2. Host validators read source + generated artifacts and fail on invalid geometry/material budgets.
3. Runtime loads `.lvl` for gameplay systems and `.t3dm` for visible static geometry.
4. Debug tooling reads bake reports to compare source counts, emitted render counts, and rejected geometry.

## Sample rows / records

```txt
room_id: 1-1
gameplay_path: rom:/lvl/1-1.lvl
render_path: rom:/lvl/1-1.t3dm
materials: [rock_1, snow_1, rock_2, metal_floor_1, floor_dirty_concrete, TB_empty]
material_refs_live_in: 1-1.t3dm
```

```txt
entity_brush_policy:
  worldspawn:
    render_mode: static_mesh
    collision_mode: solid
  Decoration:
    render_mode: audit_decides(static_mesh | none)
    collision_mode: audit_decides(none | solid)
  SpikeBlock:
    render_mode: actor_model
    collision_mode: actor_owned
  TrafficBlock:
    render_mode: actor_model
    collision_mode: actor_owned
  DeathBlock:
    render_mode: none
    collision_mode: trigger
```

## Migration plan

1. Add validators and reports without changing runtime behavior.
2. Lock the migration policy: LVL1 stays on disk for this plan; no LVL2 schema migration happens here.
3. Introduce the `.t3dm` render sidecar while `.lvl` still carries legacy face data.
4. Build and load the first `.t3dm` room beside the existing `.lvl` room.
5. Cut gameplay rendering over to `.t3dm` only after visual and runtime checks pass.
6. Quarantine the LVL1 face/vertex render path behind explicit debug-only use; schema removal is a later plan.

## Backwards-compatibility window

During migration, existing `.lvl` files remain loadable and keep their face stream. The scene can retain a debug/legacy render switch until the `.t3dm` path is proven in Ares. This overhaul does **not** introduce LVL2 or remove LVL1 fields.

## Backfill

Required for current rooms only. Regenerate affected room artifacts deterministically from source assets once the new validators and render pipeline exist.

## Rollback

- Before cutover: delete the `.t3dm` sidecar and keep using the legacy room renderer.
- After cutover: restore the debug/legacy switch while preserving `.lvl` gameplay data.
- Do not delete legacy `.lvl` face fields in this plan; a later schema plan can decide whether LVL2 is worth the migration.
