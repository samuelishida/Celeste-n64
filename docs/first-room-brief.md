# First Room Brief

Room `first-room` is the project-owned replacement for imported `1-1`. It serves
Milestone 2: one compact challenge space that teaches movement before more rooms
exist.

## Beats (Milestone 2)

| # | Beat | Description |
|---|------|-------------|
| 1 | Dash gap | A gap that requires an air dash to cross. Failure drops the player onto a lower recovery path or the kill plane. |
| 2 | Climb wall | A vertical wall tagged `_climbable`. Player must wall-grab and climb to reach the next platform. |
| 3 | Collectible route | A path with one or more pickups (strawberry / refill gem) that rewards exploration off the main line. |
| 4 | Respawn challenge | A section where falling sends the player below the kill plane, testing checkpoint reset, camera reset, and dash-refill state. |

## Room identity

```
room_id:              first-room
gameplay_source:      assets/rooms/first-room/first-room.map
render_source:        assets/rooms/first-room/first-room.glb
generated_artifacts:  first-room.lvl, first-room.colmesh, first-room.t3dm
runtime_path:         rom:/lvl/first-room.{lvl,colmesh,t3dm}
```

## Runtime policies

- **Checkpoint:** the single `PlayerSpawn` entity is the checkpoint. LVL1 does not
  currently load a separate checkpoint entity, and this room does not add one.
- **Kill plane:** uses the existing runtime default (`Room::kill_plane_y`).
  No room-authored kill-plane metadata is added by this plan.
- **Respawn:** returns the player to `PlayerSpawn` with dash refilled, velocity
  zeroed, and `prev_position` set to the spawn point.

## Brush-class policy

Every brush-bearing entity class must declare both axes before it may emit data:

| Class | Render mode | Collision mode |
|-------|-------------|----------------|
| `worldspawn` | static_mesh | solid |
| `func_wall` | static_mesh | solid |
| `func_climbable` | static_mesh | solid |
| `trigger_*` | none | trigger |
| `PlayerSpawn` | none | none |
| `Strawberry` | none | none |
| `RefillGem` | none | none |
| `SpringBoard` | none | none |

No other class may emit collision or render data. A class not listed here
defaults to `render_mode: none, collision_mode: none`.

## Collision material suffix contract

The `.map` face textures encode collision material via suffix. The baker reads
the material name and writes the corresponding flags into `.colmesh`:

| Suffix | Flag bits |
|--------|-----------|
| (default) | `MAT_SOLID` (0x0001) |
| `_oneway` | `MAT_SOLID \| MAT_ONEWAY` (0x0003) |
| `_death` / `_kill` | `MAT_SOLID \| MAT_DEATH` (0x0005) |
| `_climbable` | `MAT_SOLID \| MAT_CLIMBABLE` (0x0009) |
| `_ice` | `MAT_SOLID \| MAT_ICE` (0x0011) |
| `trigger_*` | 0x0000 (non-solid) |

The climb wall must use a material ending in `_climbable` so the runtime
`FaceIsClimbable` check passes.

## Material budget

Per-level hard caps (enforced by TMEM and DFS size):

| Resource | Budget |
|----------|--------|
| `.colmesh` size | 256 KB |
| `.t3dm` textures | ≤ 4 unique 32×32 sprites |
| `.lvl` vertices | ≤ 4096 |
| `.lvl` faces | ≤ 1024 |
| Entities | ≤ 64 |

## Shared anchors

Six named points used by acceptance fixtures to keep gameplay and render sources
aligned. All coordinates in room-local space (Y-up):

| Anchor | Purpose | Tolerance |
|--------|---------|-----------|
| `spawn` | Player start position | ±0.1 units between .map and .glb |
| `dash_takeoff` | Edge before dash gap | ±0.5 units between .map and .glb |
| `dash_landing` | Landing platform after dash gap | ±0.5 units between .map and .glb |
| `climb_wall` | Base of climbable wall surface | ±0.5 units between .map and .glb |
| `berry_route` | Midpoint of collectible path | ±0.5 units between .map and .glb |
| `kill_drop` | Point above kill plane on fall path | ±1.0 units between .map and .glb |

Anchor fixture format: JSON sidecar at `tests/fixtures/first-room-anchors.json`.
Render anchors are exported from `.glb` empties; gameplay anchors are extracted
from `.map` entity origins. The acceptance fixture compares them.

## Traversal sketch

```text
                    [berry route]
                   /
[spawn] ---- [dash gap] ---- [climb wall] ---- [respawn challenge]
                |                                     |
           [kill_drop]                          [kill_drop]
```

Player moves spawn → dash gap → climb wall → collectible detour (optional) →
respawn challenge. Falling at either kill_drop returns to spawn.
