# Madeline Cube ROM

Tiny Celeste64-inspired N64 prototype, scoped around the first playable milestone:

- one cube player
- one floating island
- analog-stick movement
- jump
- air dash
- fall respawn
- one collectible

The project is organized to fit a future Pyrite64 project layout:

```txt
assets/       source assets we own
data/         editor-authored scene data once Pyrite64 is introduced
docs/         design notes and milestone specs
src/user/     custom gameplay code we own
```

Pyrite64 should create and manage its generated project/runtime files later. The durable code in `src/user/` is kept engine-light on purpose so the movement model can be tuned before it is wired into scene scripts.

The small smoke test in `tests/gameplay_smoke.cpp` exercises the first gameplay rules without requiring the N64 toolchain yet.

## Current status

Milestone 1: Baked level pipeline + first playable room

Completed:
- ROM boots, runs, and loads baked levels (TrenchBroom → `.map` → `.lvl` + `.colmesh`)
- Player moves/jumps/dashes on collision mesh
- Multiple rooms supported (first-room stable; 1-1 WIP)
- Fall respawn to checkpoint
- Strawberry collection
- Material catalog with textured geometry

In progress:
- Stabilizing 1-1 map load (diagnostics in place for pointer corruption tracking)
- Verifying collision mesh queries under 1-1 geometry

See [`.agents/map-creation.md`](.agents/map-creation.md) for the full end-to-end pipeline (author → bake → load).

## Next build step

1. Fix remaining crash on 1-1 (diagnostics added; awaiting ROM run to isolate root cause).
2. Add more entity types (hazards, traffic blocks, moving platforms).
3. Extend room graph / level transitions.
4. Introduce scene scripting (Pyrite64 planned, hand-authored for now).

## Host-side unit tests

Core physics and collision query logic can be tested on host without the N64 toolchain:

```sh
g++ -std=c++17 -Isrc/user \
  tests/level_loader_test.cpp \
  src/user/gameplay/world/level_loader.cpp \
  -o /tmp/level_loader_test && /tmp/level_loader_test

g++ -std=c++17 -Isrc/user \
  tests/coll_mesh_query_test.cpp \
  src/user/gameplay/physics/coll_mesh.cpp \
  -o /tmp/coll_mesh_query_test && /tmp/coll_mesh_query_test
```

Note: `tests/gameplay_smoke.cpp` depends on `<libdragon.h>` via entity dispatch and cannot compile on host. Verify the full gameplay loop on ROM instead.

## ROM build

After installing the libdragon N64 toolchain and Tiny3D:

```sh
./compile-rom.sh
```

Builds `madeline_cube_rom.z64` (libdragon + tiny3d):

- Player spawns on the active room (set by `kBakedLevelPath` in `gameplay_scene.cpp`)
- Baked level geometry from TrenchBroom `.map` files
- Textured faces from material catalog
- Collision mesh queries for floor/wall detection
- Analog movement, `A` jump, `B` air dash, fall respawn
- Strawberry collectibles

To author a new room, see [`.agents/map-creation.md`](.agents/map-creation.md) for the full pipeline (TrenchBroom → bake_map.py → runtime load).

## Reference stack

- Pyrite64 for editor/runtime integration
- libdragon underneath for N64 SDK/runtime
- tiny3d underneath for 3D rendering and future GLTF assets
