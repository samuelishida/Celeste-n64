# Data model

## Scope

Database persistence is **N/A**. This plan locks the authored-room and generated-artifact model for the first project-owned room, because the bug lives at the boundary between room content, collision truth, and render truth.

## Entities & relationships

```txt
RoomBrief (project-owned design contract)
  └── room_id: first-room
      ├── GameplaySource (.map)
      │      ├── PlayerSpawn entity (also the checkpoint for this room)
      │      ├── gameplay brush classes
      │      ├── collision material suffixes
      │      └── generated LevelGameplayArtifact (.lvl)
      │             └── generated LevelCollisionArtifact (.colmesh)
      └── RenderSource (.glb)
             ├── visible static geometry
             └── generated LevelRenderArtifact (.t3dm)

AcceptanceFixture
  ├── gameplay anchors exported from room authoring data
  └── render-anchor sidecar exported from render authoring data
      (spawn, dash_takeoff, dash_landing, climb_wall, berry_route, kill_drop)
```

## Constraints & indexes

- `room_id = first-room` resolves exactly one gameplay artifact, one collision sidecar, and one render artifact: `first-room.lvl`, `first-room.colmesh`, `first-room.t3dm`.
- The `.map` source owns gameplay truth available in LVL1 today: `PlayerSpawn`, static collision surfaces, triggers, and actor spawns.
- For this room, the single `PlayerSpawn` is also the checkpoint because LVL1 currently loads no separate checkpoint entity.
- The first room uses the existing runtime kill plane; this plan does not add room-authored kill-plane metadata.
- The `.glb` source owns visible static geometry only; named validation anchors are exported to a sidecar fixture, not preserved in `.t3dm`.
- The room brief fixes the Milestone 2 beats: one dash gap, one climb wall, one collectible route, and one obvious respawn challenge.
- Gameplay brush classes are explicit; no brush becomes solid or spawn-bearing by accident.
- Collision material naming is explicit: climbable surfaces must emit a `_climbable` material so `.colmesh` carries `MAT_CLIMBABLE`.
- Collision artifacts must pass `.colmesh` structural checks, fit the existing 256 KB per-level budget, and expose floor beneath the spawn anchor.
- Render artifacts must be finite, non-degenerate, TMEM-safe, and fit the active `.t3dm` room path.
- Shared anchors must stay within an agreed tolerance between authored sources; the acceptance fixture is the review surface for drift.

## Query patterns

1. Build reads the room brief plus `.map` and writes `first-room.lvl` plus the temporary LVL1 compatibility manifest.
2. Collision bake reads `first-room.lvl` and writes `first-room.colmesh`.
3. Render conversion reads `first-room.glb` and writes `first-room.t3dm`; a companion export writes render-anchor fixture data, while render-material truth lives in `.t3dm`, not the compatibility manifest.
4. Host tests load `first-room.lvl/.colmesh` and query spawn safety, floor hits, climbability flags, kill-plane behavior, and scripted traversal anchors.
5. Runtime loads `first-room.lvl` for gameplay, `first-room.colmesh` for static queries, and `first-room.t3dm` for visible room geometry.

## Sample records

```txt
room_id: first-room
gameplay_source: assets/rooms/first-room/first-room.map
render_source: assets/rooms/first-room/first-room.glb
checkpoint_policy: player_spawn_is_checkpoint
kill_plane_policy: use_existing_runtime_default
gameplay_artifact: rom:/lvl/first-room.lvl
collision_artifact: rom:/lvl/first-room.colmesh
render_artifact: rom:/lvl/first-room.t3dm
```

```txt
anchors:
  spawn:          { purpose: player_start }
  dash_takeoff:   { purpose: dash_gap_entry }
  dash_landing:   { purpose: dash_gap_exit }
  climb_wall:     { purpose: wall_grab_test }
  berry_route:    { purpose: collectible_route }
  kill_drop:      { purpose: respawn_challenge }
```

## Migration plan

1. Add the room brief and anchor fixture without changing the shipping room.
2. Add project-owned gameplay and render sources beside the imported `1-1` assets.
3. Generate `first-room.lvl`, `first-room.colmesh`, `first-room.t3dm`, and render-anchor fixture data; keep imported `1-1` assets loadable as reference while the new room is validated.
4. Cut runtime boot over to `first-room` only after collision fixtures and the active `.t3dm` room path pass.
5. Remove shipping-build dependence on imported `1-1` artifacts after the new room is accepted.

## Backwards-compatibility window

During migration, both `1-1` and `first-room` artifacts may exist in the repo. Runtime should boot one room at a time; imported `1-1` remains a reference/debug artifact until the cutover increment lands. LVL1 stays intact; this plan does not introduce LVL2.

## Backfill

Required once for the new room only: generate all three artifacts deterministically from the new authored sources and add them to DFS packaging. No historical save data or user data exists.

## Rollback

- Before runtime cutover: drop the new `first-room` DFS entries and continue booting `1-1`.
- After runtime cutover: restore the prior boot target and keep the new artifacts on disk for debugging.
- Do not delete `Celeste64-og/` or imported OG assets; they remain reference material even after shipping no longer depends on them.
