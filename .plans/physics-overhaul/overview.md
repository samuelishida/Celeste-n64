# Physics & Collision Overhaul

## What & why

Replace graybox `Box`/`Plane` colliders (currently 512-cap array in `Room`) with
a baked triangle-mesh collision system plus a fixed-step integrator. Existing
`PlayerMotor` / `Room` / `GroundHit-WallHit-CeilingHit` query shapes are
preserved; the substrate underneath them changes. tiny3d itself stays
render-only — collision lives in a sibling `physics/` layer, fed by a
`.colmesh` sidecar emitted from the level export pipeline. Goal: parity with OG
Celeste 64 collision feel (walk/jump/dash/climb/spring/spike/oneway, moving
platforms) on real triangle geometry, with deterministic 60 Hz simulation.

## Increment DAG

- Inc 1 — Geom primitives + math bridge (S) — depends on: none — unblocks: 2, 3, 4
- Inc 2 — `.colmesh` format + importer emit (M) — depends on: 1 — unblocks: 3, 4
- Inc 3 — Runtime BVH build + queries (M) — depends on: 2 — unblocks: 4
- Inc 4 — World query rewrite over CollMesh (L) — depends on: 3 — unblocks: 5, 6
- Inc 5 — Fixed-step 60 Hz integrator + render interp (M) — depends on: 4 — unblocks: 6
- Inc 6 — Motor cutover + climb/wall classification (M) — depends on: 4, 5 — unblocks: 6b
- Inc 6b — Bulk bake + manifest flip (S) — depends on: 6 — unblocks: 7
- Inc 7 — Drop legacy Box/Plane path (S) — depends on: 6b — unblocks: —

```
  1 ──► 2 ──► 3 ──► 4 ─┬─► 5 ─┐
                      │      ├─► 6 ──► 6b ──► 7
                      └──────┘
```

## Top 3 risks

- **Collision mesh ROM size** — triangle soup + BVH per level may blow cart budget. Mitigation: quantize positions to int16 like render mesh; measure on largest level in Inc 2 before committing format.
- **Swept-sphere vs triangle edge/vertex cases** — incorrect contact normal at mesh seams causes player to snag. Mitigation: extensive unit fixtures in Inc 1; reuse seam-tolerant algorithm (Ericson chapter 5).
- **Fixed-step + render interpolation regression** — current variable-dt code paths assume immediate post-move state. Mitigation: Inc 5 introduces interp behind a config flag; cutover only after motor parity tests pass.

## Files

- [data-model.md](data-model.md) — `.colmesh` file format, runtime CollMesh struct, BVH layout, migration plan
- [plan.md](plan.md) — increment list with dependencies and acceptance checks
- [decisions.md](decisions.md) — architectural choices, sourced assumption log
- [verification.md](verification.md) — end-to-end acceptance scenarios
- [contracts.md](contracts.md) — new/changed query APIs
