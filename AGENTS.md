# AGENTS.md

Project guide for future coding agents working on the `Madeline Cube ROM` prototype.

## Project intent

This is **not** a direct port of Celeste64. It is a small Celeste64-inspired Nintendo 64 homebrew rewrite that uses the original game as design reference while targeting real N64-friendly technology.

Current strategy:

- use `libdragon + tiny3d` for the first real ROMs
- keep Pyrite64 as the likely future editor/runtime layer once scene authoring and imported assets matter more than hand-built placeholders
- prove movement first, then add art, then grow scope one room at a time

The original Celeste64 project has been copied into `Celeste64-og/` for reference. Treat it as reference material, not as code to port wholesale.

## Current status

Milestone 0 is alive and booting as a ROM.

Implemented:

- blue cube player
- green floating island
- red collectible cube
- analog movement
- `A` jump
- single airborne `B` dash
- third-person follow camera
- fall respawn
- simple pickup detection

Generated ROM:

- `madeline_cube_rom.z64`

Important current behavior:

- movement uses `+Y` as up and the `XZ` plane for travel
- the dash uses current move input, or the last facing direction if there is no current input
- touching ground refills the air dash
- falling below the kill plane respawns at the checkpoint
- controller `X` input is intentionally negated in `src/user/rom_main.cpp`; the first test ROM had left/right mirrored until this was fixed

## Repository map

```txt
assets/                     future source assets
Celeste64-og/               original game reference checkout
data/scenes/                future editor-authored scene data
docs/                       design notes, budgets, milestone notes
src/user/gameplay/          engine-light gameplay code (see subdirs below)
src/user/gameplay/actor/    actor base + spring, refill, strawberry, bobbing
src/user/gameplay/player/   player controller, motor, camera, state, config
src/user/gameplay/world/    world, rooms, collectibles, respawn
src/user/gameplay/scene/    scene graph, manager, gameplay scene
src/user/rom_main.cpp       current hand-authored N64 demo entrypoint
tests/                      host-side smoke tests
Makefile                    current libdragon/tiny3d ROM build
```

Key docs:

- `README.md` - quick project overview
- `docs/milestones.md` - milestone ladder
- `docs/movement_spec.md` - current movement constants and behavior
- `docs/asset_budget.md` - first art constraints
- `docs/stack_examples.md` - examples of modern projects on related tooling

## Architecture notes

The current code intentionally separates gameplay rules from the engine-facing ROM entrypoint.

Gameplay layer in `src/user/gameplay/`:

- `actor/` - Actor base, BobbingActor, SpringActor, RefillActor, StrawberryActor
- `player/` - player_controller, player_motor, camera_controller, player_state, movement_config
- `world/` - world, room_data, collectible, respawn_system
- `scene/` - scene, scene_manager, gameplay_scene
- Root: math_types, physics_contracts, arena, save_system, placeholder_catalog, debug_hud, rom_telemetry

ROM layer in `src/user/rom_main.cpp`:

- initializes libdragon and tiny3d
- creates cube geometry manually
- reads N64 controller input
- resolves the placeholder island collision
- updates gameplay systems
- draws the island, player, and collectible

Keep this separation unless there is a strong reason not to. It makes tuning and testing easier while the N64-facing code is still young.

## Build prerequisites

The project currently builds against:

- libdragon `preview`
- tiny3d
- a MIPS64 GCC toolchain compatible with current libdragon

The first successful build used a **temporary** toolchain under `/tmp`:

```txt
/tmp/n64-toolchain-root/opt/libdragon
/tmp/libdragon-preview
/tmp/tiny3d
```

That setup is reproducible, but not persistent. If `/tmp` is cleaned, `make` will fail because `$(N64_INST)/include/t3d.mk` no longer exists.

Longer term, prefer installing the toolchain in a durable location. Until then, use the bootstrap recipe below.

## Cold-start bootstrap

Run these commands from a shell with network access:

```sh
curl -L \
  https://github.com/DragonMinded/libdragon/releases/download/toolchain-continuous-prerelease/gcc-toolchain-mips64-x86_64.deb \
  -o /tmp/gcc-toolchain-mips64-x86_64.deb

mkdir -p /tmp/n64-toolchain-root
dpkg-deb -x /tmp/gcc-toolchain-mips64-x86_64.deb /tmp/n64-toolchain-root

git clone --depth 1 --branch preview \
  https://github.com/DragonMinded/libdragon.git \
  /tmp/libdragon-preview

git clone --depth 1 \
  https://github.com/HailToDodongo/tiny3d.git \
  /tmp/tiny3d
```

Build libdragon:

```sh
cd /tmp/libdragon-preview
N64_INST=/tmp/n64-toolchain-root/opt/libdragon \
PATH=/tmp/n64-toolchain-root/opt/libdragon/bin:$PATH \
./build.sh
```

Build/install tiny3d into the same toolchain:

```sh
cd /tmp/tiny3d
N64_INST=/tmp/n64-toolchain-root/opt/libdragon \
PATH=/tmp/n64-toolchain-root/opt/libdragon/bin:$PATH \
./build.sh
```

## Project build

From the repo root:

```sh
N64_INST=/tmp/n64-toolchain-root/opt/libdragon \
PATH=/tmp/n64-toolchain-root/opt/libdragon/bin:$PATH \
make
```

Expected output:

```txt
madeline_cube_rom.z64
```

Clean build artifacts:

```sh
N64_INST=/tmp/n64-toolchain-root/opt/libdragon \
PATH=/tmp/n64-toolchain-root/opt/libdragon/bin:$PATH \
make clean
```

## Local smoke test

The host-side gameplay smoke test does not need the N64 toolchain:

```sh
g++ -std=c++17 -Isrc/user/gameplay \
  tests/gameplay_smoke.cpp \
  src/user/gameplay/player_controller.cpp \
  src/user/gameplay/collectible.cpp \
  src/user/gameplay/respawn_system.cpp \
  -o /tmp/madeline_cube_smoke

/tmp/madeline_cube_smoke
```

Current smoke coverage:

- grounded jump
- air dash consumption
- collectible pickup
- kill-plane respawn reset

## ROM testing notes

Known local result:

- `mupen64plus` successfully recognized and launched `madeline_cube_rom.z64`

Recommended validation habit:

- use Mupen64Plus for a quick local smoke launch if convenient
- use Ares or gopher64 for serious validation of modern libdragon/tiny3d behavior

Current ROM control map:

- analog stick: move
- `A`: jump
- `B`: dash

First field issue already found:

- left/right were mirrored in the first ROM build
- fix: negate `held.stick_x` in `ReadPlayerInput()`
- if future movement feels wrong, check axis conventions at the input boundary before changing physics

## What we learned so far

### Scope and stack

- Directly porting the C# Celeste64 code is the wrong job; the N64 version should be a rewrite.
- `libdragon + tiny3d` is enough to produce a real ROM immediately.
- Pyrite64 is still attractive, but the first movement prototype did not need editor integration.
- Starting one layer lower was useful because it proved the ROM, rendering loop, controller input, and gameplay loop before any scene tooling work.

### Build and tooling

- The current libdragon `preview` flow plus tiny3d builds successfully on this machine.
- The build environment works, but storing it in `/tmp` is fragile.
- The top-level `Makefile` is intentionally small and only compiles project-owned C++ sources.
- Building full libdragon/tiny3d from scratch also builds many examples, so a cold bootstrap is slower than normal project iteration.

### Code and gameplay

- Keeping gameplay rules engine-light paid off immediately; we had a host smoke test before the first ROM existed.
- The current placeholder collision is intentionally crude: one axis-aligned island and one kill plane.
- Camera, collision, input, and gameplay are still tightly simple on purpose. Avoid polishing abstractions before Milestone 1 exposes real needs.
- The current first-pass movement values are in `docs/movement_spec.md`; tune by feel only after checking behavior in ROM.

### Reference material

- `Celeste64-og/` is useful for movement feel, level ideas, and asset/legal reference.
- The original Celeste64 asset/content rights are separate from the MIT source code, so public releases should avoid casually shipping original content unless permission/release strategy is clear.

## Immediate next milestones

Milestone 1 should focus on feel, not art:

- coyote time
- jump buffering
- variable jump height decision
- dash reset/cooldown tuning
- better friction and air control
- first wall-grab experiment
- verify stick axes and camera-relative controls before adding complexity

Milestone 2 should replace the hand-built island with one compact authored room.

## Agent working rules for this repo

- Preserve the current gameplay/ROM separation.
- Prefer small, testable changes over giant engine rewrites.
- Do not treat `Celeste64-og/` as shippable content by default.
- Rebuild the ROM after N64-facing changes.
- Re-run the host smoke test after gameplay changes.
- When controls feel wrong, inspect coordinate-system boundaries first:
  - controller input
  - camera orientation
  - world-space movement mapping
- Keep the first public goal narrow: one room, one character, a few mechanics, good feel.

