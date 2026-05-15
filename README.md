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

## Current milestone

Milestone 0: `Madeline cube ROM`

Definition of done:

1. ROM boots.
2. Player moves on a simple platform.
3. `A` jumps.
4. `B` dashes once in air.
5. Falling below the kill plane respawns the player.
6. Touching one strawberry cube marks it collected.

## Next build step

1. Create a new Pyrite64 project in this folder.
2. Recreate the scene from [`docs/milestones.md`](docs/milestones.md).
3. Bind Pyrite64 object scripts to the gameplay modules in `src/user/gameplay/`.
4. Replace placeholders only after the movement loop feels good.

## Local smoke test

Until Pyrite64 is wired in, the engine-light gameplay layer can be checked with:

```sh
g++ -std=c++17 -Isrc/user/gameplay \
  tests/gameplay_smoke.cpp \
  src/user/gameplay/player_controller.cpp \
  src/user/gameplay/collectible.cpp \
  src/user/gameplay/respawn_system.cpp \
  -o /tmp/madeline_cube_smoke
/tmp/madeline_cube_smoke
```

## ROM build

After installing the current libdragon preview toolchain and Tiny3D:

```sh
make
```

This builds `madeline_cube_rom.z64`, a hand-authored `libdragon + tiny3d` version of Milestone 0:

- blue cube player
- green floating island
- red collectible cube
- analog movement
- `A` jump
- `B` air dash
- fall respawn

This first ROM is intentionally below the Pyrite64 editor layer. Pyrite64 is still the planned project path once we start authoring scenes and imported assets instead of placeholder geometry in code.

## Reference stack

- Pyrite64 for editor/runtime integration
- libdragon underneath for N64 SDK/runtime
- tiny3d underneath for 3D rendering and future GLTF assets
