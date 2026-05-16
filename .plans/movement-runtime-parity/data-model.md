# Data model

## Entities & relationships

This plan does not change save data. It introduces a richer runtime model for player motion, collision queries, and parity evidence.

```text
Room
  -> CollisionSurface[*]
  -> MovingSurface[*]

PlayerState
  -> ContactState
  -> PlatformCarryState
  -> ClimbState

PhysicsWorld
  -> RayHit
  -> WallHit
  -> SweepResult

ParityScenario
  -> TraceFrame[*]
  -> ToleranceProfile
```

| Entity | Purpose |
|---|---|
| `CollisionSurface` | Queryable static floor, ceiling, wall, or slope surface baked from graybox room data. |
| `MovingSurface` | Queryable platform surface with frame velocity and rider carry metadata. |
| `ContactState` | Per-frame grounded flag, ground normal, floor handle, coyote height, previous velocity, and ground-snap cooldown. |
| `PlatformCarryState` | Stored platform velocity plus expiry timer used by jumps and platform handoff. |
| `ClimbState` | Wall normal, wall handle, corner interpolation data, cooldowns, and camera-facing helpers for the climb state. |
| `RayHit` / `WallHit` / `SweepResult` | Deterministic physics query outputs consumed by the player motor and camera. |
| `ParityScenario` / `TraceFrame` | Versioned host-side movement fixtures captured from the C# reference and compared against the N64-side controller model. |

## Constraints & indexes

- The prototype remains fixed-step at 60 Hz; every parity trace stores one row per simulation tick.
- Hot-path runtime data stays fixed-capacity and allocation-free after room load so stock 4 MB profiling remains meaningful.
- Surface normals are normalized at bake/load time; floor surfaces satisfy `normal.y > floor_threshold`, climbable walls satisfy `abs(normal.y) < climb_threshold` after adapting the source game's `+Z` up convention to this port's `+Y` up convention.
- Physics query ordering is deterministic: nearest distance first, then stable surface ID as the tie-breaker.
- `RayHit`, `WallHit`, and `SweepResult` must carry enough information to avoid a second broad query in the same tick: hit point, normal, pushout, distance, surface handle, and moving-surface velocity where applicable.
- `ParityScenario` IDs are semantic and stable, for example `jump_full_hold`, `dash_ground_exit`, `skid_reverse`, `wall_jump_probe`, and `platform_takeoff`.
- `TraceFrame` rows are keyed by `(scenario_id, frame_index)`. Comparators read sequentially; no runtime index is needed in the ROM.
- No persistent indexes or save migrations are added by this plan.

## Query patterns

1. Each player tick performs floor, ceiling, and wall queries while sweeping the requested movement delta in bounded substeps.
2. After motion, the late-update phase rechecks ground contact, optionally snaps to nearby floor, and refreshes coyote and dash-reset state.
3. Wall jump and climb entry query the nearest compatible wall normal independently of wall-grab state.
4. Camera follow uses the same raycast layer to shorten obstructed camera paths and ceiling-clear the final camera position.
5. Host-side parity tests replay each scenario, emit `TraceFrame` rows, and compare them against the committed source trace plus scenario-specific tolerances.

## Sample rows

`CollisionSurface`

| surface_id | kind | point | normal | bounds | moving |
|---:|---|---|---|---|---|
| 12 | floor | `(0, 0, 0)` | `(0, 1, 0)` | `[-10,0,-10]..[10,0,10]` | `false` |

`ContactState`

| grounded | ground_normal | coyote_height | previous_velocity | snap_cooldown |
|---|---|---:|---|---:|
| `true` | `(0, 1, 0)` | `1.0` | `(5.8, -9.2, 0)` | `0.0` |

`TraceFrame`

| scenario_id | frame | position | velocity | state | grounded |
|---|---:|---|---|---|---|
| `dash_ground_exit` | 14 | `(2.18, 1.00, 0.00)` | `(14.00, 0.00, 0.00)` | `Dashing` | `true` |

`ToleranceProfile`

| scenario_id | position_epsilon | velocity_epsilon | exact_events |
|---|---:|---:|---|
| `jump_full_hold` | `0.15` | `0.20` | `takeoff_frame, apex_frame, landing_frame` |

## Migration plan

- Save/data migration is N/A: this work only changes runtime structures and host-side fixtures.
- Runtime migration happens incrementally in code: introduce the query layer, add the phased player loop, then move states onto the new motor before deleting the coarse resolver path.
- The old smoke coverage stays green while the richer fixtures are introduced; the final parity increment removes obsolete assumptions only after replacement tests exist.

## Backwards-compatibility window

- Save compatibility is N/A: no cartridge format changes.
- During implementation, the existing graybox scene remains playable at the end of every increment.
- The old broad smoke tests may coexist with the new parity harness until each scenario has a direct replacement.

## Backfill

- N/A for persistent data.
- Reference trace fixtures are generated once for the chosen C# source revision, committed, and regenerated only when the behavioural target intentionally changes.

## Rollback

1. Before the new motor owns gameplay, revert only the adapter calls and keep the query layer/tests.
2. After the new motor owns gameplay, revert one increment at a time in reverse dependency order: tuning, camera coupling, advanced surfaces, wall/climb, locomotion, motor.
3. If a reference fixture is wrong, revert the fixture update and keep the failing comparator so later tuning does not silently target bad data.
