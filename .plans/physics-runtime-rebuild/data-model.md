# Data model

## Entities & relationships

This plan does not change save data. It locks the runtime contracts that the repaired physics stack must share before more controller work lands.

```text
ReferenceRun
  -> ReferenceFrame[*]

Room
  -> CollisionFace[*]
  -> MovingSurface[*]

PhysicsWorld
  -> RayHit
  -> WallHit
  -> GroundHit

PlayerState
  -> ContactState
  -> PlatformCarryState
  -> ClimbState

MotorFrame
  -> MotorInput
  -> MotorResult
```

| Entity | Purpose |
|---|---|
| `ReferenceRun` / `ReferenceFrame` | Captured C# movement truth for one semantic scenario at one fixed source revision. |
| `CollisionFace` | Queryable polygon/plane face with stable ID, normal, material flags, and optional owning surface. |
| `MovingSurface` | Collision owner with per-frame displacement and rider velocity storage metadata. |
| `RayHit` / `WallHit` / `GroundHit` | Source-shaped query outputs consumed by player, camera, climb, and platform logic. |
| `ContactState` | Grounded state, previous grounded state, ground normal, coyote height, and ground-snap cooldown. |
| `PlatformCarryState` | Stored platform velocity plus expiry timer. |
| `ClimbState` | Active wall normal/owner plus corner interpolation and cooldown fields. |
| `MotorInput` / `MotorResult` | One-frame boundary between state logic and movement resolution. |

## Constraints & indexes

- Persistence schema is unchanged; there are no save migrations or persistent indexes.
- Runtime data stays fixed-capacity and allocation-free after room load to preserve the stock 4 MB target.
- Reference frames are keyed by `(scenario_id, frame_index)` and record position, velocity, movement state, contact flags, and named events.
- Scenario IDs are semantic, not asset-based: `fall_to_floor`, `jump_full_hold`, `dash_ground_exit`, `wall_jump_probe`, `climb_corner`, `platform_takeoff`.
- Collision face IDs are stable within a room build so deterministic tie breaking can be tested.
- Source coordinates remain `+Z` up inside captured reference data; the N64 runtime exposes `+Y` up. The adapter boundary is explicit and tested.
- `RayHit`, `WallHit`, and `GroundHit` carry hit point, normal, distance/pushout, face ID, owner ID, and owner velocity so callers do not issue follow-up guesses.
- `MotorResult` is the sole writer of post-move position/contact resolution. State code may request movement; it may not repair motion afterward.

## Query patterns

1. Capture runner records C# frames for fixed input scripts and exports deterministic reference files.
2. Each motor tick sweeps requested displacement in bounded substeps, resolves source-shaped popout checks, then emits contact results.
3. Late contact refresh performs ground check, optional ground snap, coyote refresh, dash refill, and landing event detection.
4. Wall jump and climb entry query compatible wall normals independently of current grab state.
5. Camera, ledge steering, and platform riding reuse the same world queries instead of bespoke collision probes.
6. ROM diagnostics sample spawn, first landing, active collider count, numeric validity, and motor phase state for hardware comparison.

## Sample rows

`ReferenceFrame`

| scenario_id | frame | position_src | velocity_src | state | grounded | events |
|---|---:|---|---|---|---|---|
| `fall_to_floor` | 18 | `(0, 0, 0)` | `(0, 0, 0)` | `Normal` | `true` | `land` |

`CollisionFace`

| face_id | owner_id | kind | point | normal | collidable |
|---:|---:|---|---|---|---|
| 12 | 3 | `floor` | `(0, 0, 0)` | `(0, 1, 0)` | `true` |

`GroundHit`

| hit | point | normal | distance | face_id | owner_velocity |
|---|---|---|---:|---:|---|
| `true` | `(0, 0, 0)` | `(0, 1, 0)` | `0.42` | 12 | `(0, 0, 0)` |

`MotorResult`

| position | velocity | grounded | wall_contact | landed_this_frame |
|---|---|---|---|---|
| `(0, 1, 0)` | `(0, 0, 0)` | `true` | `none` | `true` |

## Migration plan

- Save/data migration is N/A because only runtime structures and test fixtures change.
- Runtime migration happens in phases: add the capture oracle and diagnostics, introduce the repaired query contracts, port the motor behind the contracts, then move higher-level states onto the rebuilt motor before deleting compatibility code.
- The current source-derived fixtures remain only until captured C# fixtures replace each covered scenario.

## Backwards-compatibility window

- Save compatibility is unchanged.
- The graybox route must remain bootable at the end of every increment.
- Existing controller tests may coexist with replacement parity tests only while they still assert behavior the C# capture agrees with.

## Backfill

- Persistent backfill is N/A.
- Fixture backfill is required: every existing modeled trace must be replaced by an actual C# capture or explicitly retired as non-authoritative.

## Rollback

1. Before the rebuilt motor owns gameplay, revert adapter calls and keep the capture/diagnostic tooling.
2. After motor cutover, roll back in reverse dependency order: advanced states, core locomotion, motor, query layer.
3. If a captured reference run is wrong, revert that fixture and keep the failing comparator so tuning cannot silently target fabricated behavior.
