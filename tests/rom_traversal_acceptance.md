# ROM Traversal Acceptance (Inc 10 / Inc 3 gate)

This document records the exact Ares/Mupen64Plus procedure and expected logs
for the full Forsaken City A-side `1.map` traversal. It is the **authoritative
acceptance gate** for the interconnected map render fix — the recurring
failure across prior plans was "DONE" validated on host tests or a single
static chunk, never on real traversal across seams.

## Build

```sh
./setup.sh                 # cold-start bootstrap (if toolchain missing)
./compile-rom.sh           # builds madeline_cube_rom.z64
```

The ROM boots the full A-side map-pack v2 from the manifest `Start` spawn.

## Expected boot logs

```
[mappack] v2 loaded rom:/lvl/forsyken-city/forsyken-city.mappack: 45 rooms, start=cell_00_00, global=rom:/lvl/forsyken-city/forsyken-city.colmesh (10618 tris)
[map_runtime] global collision loaded: 10618 tris, 20868 verts
[map_runtime] active room -> cell_00_00 (origin ...)
```

- Boot room: `cell_00_00`
- Start position: the manifest `Start` spawn (world coords)
- Global mesh identity: the one `forsyken-city.colmesh` (never swapped/null)

## Telemetry line (every 60 frames)

```
[telemetry] f=120 sp=1 rp=0 g=120 a=0 w=0 ip=0 iv=0 st=1 L=0 Ds=0 De=0 W=0 C=0 ms=0 sl=0 sn=0 room=cell_00_00 fnorm=1.000 orig=(-120.0,0.0,-120.0) pos=(0.000,35.800,89.600) vel=(0.000,0.000,0.000)
```

| Field | Meaning | Pass condition |
|-------|---------|----------------|
| `room=` | active room id | changes when crossing a seam |
| `fnorm=` | floor normal Y | near `1.000` when grounded |
| `orig=` | active room render origin | matches the room's manifest origin |
| `pos=` | player world position | stays above the floor (no fall-through) |
| `g=` | grounded frames | stays high while walking (no fall-through) |

## Traversal route

Execute a deterministic replay/route that:

1. Visits every Start-reachable visual room (all 45).
2. Crosses every reachable manifest edge with player-radius floor/wall/sweep
   probes.
3. Includes at least one +X seam and one world-Z/depth seam, and crosses back.
4. Collects an actor (strawberry) in a non-start room.
5. Falls/dies and verifies the explicit `Start` checkpoint is restored.

## Expected logs during traversal

- Active room changes: `[map_runtime] active room -> <cell_id> (origin ...)`
- Collision normal: floor probes report `ground_normal.y ≈ 1.0`
- No-null-mesh transitions: `[map_runtime] global collision loaded` appears
  exactly once (the mesh is never reloaded or nulled)
- Render-origin values: each active room logs its `(origin ...)` (cell center)
- Global-mesh identity: `room.coll_mesh` always equals the global mesh pointer
- Checkpoint respawn: after death, `[map_runtime] active room -> cell_00_00`
  (the Start room) and the player resets to the Start spawn
- Neighbor ring: the render-only ring (active cell + 4 neighbors) draws each
  frame; crossing a seam shows the next chunk without a hitch

## Acceptance criteria

- No fall-through (player never passes through the global floor).
- No stale-room collision (queries always use the global mesh).
- No renderer truncation (`[lvlroom] ... discarded=0` for every room).
- The measured N64 global-collision memory gate passes (resident CMSH +
  staging renderer + actor storage + query scratch < declared budget).
- No artifact/hash mismatch in the ROM's DFS (the v2 loader rejects stale
  files).
- Crossed ≥2 seams (one +X, one world-Z/depth) and crossed back, with
  `fnorm≈1.000` and `g` high on both sides of each seam.

## Emulator

- Mupen64Plus: quick local smoke launch.
- Ares or gopher64: serious validation of modern libdragon/tiny3d behavior.
