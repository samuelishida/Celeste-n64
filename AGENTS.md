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
- OG Celeste64 wall climb (`.plans/climb-wall-physics/` + `.plans/climb-and-zoom-fix/` + `.plans/wall-climb-overhaul/` + `.plans/climb-parity-3/`):
  unlimited grip on any solid wall (stamina removed — OG has none), gravity-free
  kinematic wall movement, ballistic ledge-hop mantle, SET-style climb walljump.
  Climb now runs OG's SINGLE wall-check family (plane-based, waist-anchored
  radius 4, `ClimbNormalCheck` gate) for grip, ledge, and corners — the old
  sphere/plane two-query split and the motor's waist-flag OR are gone. Entry is
  OG `TryClimb` parity (own waist query, re-anchors to `wall.point + normal*3`,
  airborne +4 snap-up). The full OG corner system is ported: inner-corner side
  ray, slope-away snap ray, outer-corner forward+side ray with a 0.2 s eased
  position/facing lerp. Rays stay on the player's side of finite faces; an
  unreachable back-side candidate is rejected so the player releases at an
  unsupported edge instead of snapping through rock. `wall_contact` is
  physical-only (body-center sphere).
  Normal and climb physics use OG's 3-unit wall radius/pushout and 11.25-unit
  center-to-feet half-height; the static model bounds (radius 8.61, height
  21.86) do not change movement or grab reach. Renderer applies a separate
  0.5-unit minimum model gap near walls, including both faces during corner
  easing. `Popout` keeps **impact-only** full-3D velocity removal and full-3D
  positional popout (Y capped ±6). This removes the
  grab→release→fall→regrab pump on the baked map. Camera zoom deviates from OG
  deliberately: `zoom_rate` 1.8/s + `zoom_snap_speed` 1.0/s (was implicit 1.0/s
  and 0.5)
  because the N64 stick medium lacks a mouse wheel; while zoom is active the
  camera-position follow tightens (`0.0001` vs resting `0.01`) so the perceived
  zoom isn't gated by the ~7%/frame follow lag. Orbit/follow/FOV stay OG-exact
  otherwise.
- **Single-mover overhaul** (`.plans/og-mover-overhaul/`): movement/collision now
  use one OG-shaped `SweepTestMove` + `Popout` implementation via `MoveService`
  (`player_movement.{hpp,cpp}`). A tick may submit ordered climb and velocity
  sweeps, followed by one final popout; contacts accumulate across them. The
  old `PlayerMotor` (four parallel repair paths) is DELETED. Host and real-map
  fixtures cover the tested wall/floor routes; this does not guarantee every
  mesh edge case. OG feel ported: Skidding state, dash rotate-steering, 0.10 s dash
  input buffer, OG 0.10 s jump buffer, slope max-speed modulation, analog
  magnitude map. Host fixtures + `[move]` device telemetry validate.

Generated ROM:

- `madeline_cube_rom.z64`

Important current behavior:

- movement uses `+Y` as up and the `XZ` plane for travel
- the dash uses current move input, or the last facing direction if there is no current input
- touching ground refills the air dash
- falling below the kill plane respawns at the checkpoint
- controller `X` input is intentionally negated in `src/user/rom_main.cpp`; the first test ROM had left/right mirrored until this was fixed

The main world is now the whole Forsaken City A-side `1.map`, baked through
the **canonical world IR** (`tools/ogworld/`) into one global collision mesh +
per-cell LVL2 visual rooms + a **map-pack v4** manifest (MPP2/MPP3 remain readable)
(`filesystem/lvl/forsyken-city/`, 46 cells at `--chunk-size 1200`). The
runtime (`MapRuntime` + `WorldCollision`) owns ONE global CMSH for the map
lifetime and one authoritative active visual room; all static queries resolve
through the global mesh, and the active room exposes a compatibility pointer
to it. **Axis convention (load-bearing):** the grid is 2D in WORLD XZ
(cell `iz = floor(world_z / (chunk_size·scale))`, world_z = depth = −map_y) —
never map_z, which is the Quake UP axis. Boot uses the manifest `Start` spawn
record. Render origins are per-cell centers so the full map's absolute
coordinates do not overflow the int16 fixed-point packing.

**Renderer (near + distant z-split).** The runtime draws the sky, then the
frustum-culled DLOD horizon with its own projection, clears depth, and draws
the detailed near pass (near=5/far=800). Three independently buffered Tiny3D
viewports keep sky, distant, and near matrices stable while the RSP consumes
them. Residency remains the bounded 13-cell set: the 3×3 Chebyshev-1 core plus
up to four camera-forward Chebyshev-2 cells. Near submission and DLOD culling
use the same conservative 3D AABB/frustum predicate and margin. DLOD yields
only to ready cells the near pass actually submits; pending or off-frustum
cells remain horizon-owned when visible to DLOD. Outer residency re-ranks
after camera turns cross a heading sector, with a dead band to prevent load
churn. Geometric reveal remains off. Dynamic-platform visibility uses
conservative 3D camera-frustum tests, plus 48u player and 64u look-target
safety margins. Incremental `ResolveRingDiff` and global material grouping remain.
Ares visual/FPS qualification is required for each new ROM; do not infer split
correctness or 30 fps from host tests.
Telemetry reports `distant`/`high_priority`/`streaming` phases and
`distant_cells`/`distant_faces`/`distant_batches`/`distant_vert_loads`/
`distant_syncs`, separately from near `near_batches`/`texture_uploads`/
`vert_loads`/`syncs` counters.

**Texturing (UV wrap).** LVL2 UVs are **unwrapped world-scale texel units** (1
unit = 1 texel), packed as s10.5 with **`1 texel = 32` units** (`u * 32.0f`).
The RDP must **wrap** the tile (not clamp), so `TexturedRoomRenderer` forces
`REPEAT_INFINITE` texparms (`kWrapTexparms`) on both sprite uploads; the int16
overflow of extreme unwrapped UVs is harmless because 65536 is a multiple of
the 1024-unit tile size. Boot orients the player + camera toward the map center
via `CameraController::OrientForward` so the map is on-screen at spawn (the
default +Z facing would point away from a -Z map).

Bake: `make bake-forsaken-city` (or `python3 tools/bake_interconnected_map.py
assets/og_converted/maps/1.map --out-dir build/bake-fc-1200 --chunk-size 1200
--scale 0.2 --mappack-id forsyken-city`).

## Repository map

```txt
assets/                     future source assets
Celeste64-og/               original game reference checkout
data/scenes/                future editor-authored scene data
docs/                       design notes, budgets, milestone notes
src/user/gameplay/          engine-light gameplay code (see subdirs below)
src/user/gameplay/actor/    actor base + spring, refill, strawberry, bobbing
src/user/gameplay/player/   player controller, motor, camera, state, config
src/user/gameplay/world/    world, rooms, collectibles, respawn, map_runtime
src/user/gameplay/scene/    scene graph, manager, gameplay scene
src/user/rom_main.cpp       current hand-authored N64 demo entrypoint
tools/ogworld/              canonical world IR + class policy + geometry/chunking
tools/writers/              colmesh/lvl/t3dm/nav writers (incl. world writers)
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
- `player/` - player_controller, player_movement (single mover; PlayerMotor deleted), player_state, movement_config, camera_controller
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

The toolchain bytes are stored in-repo (`.n64-bootstrap`, git-ignored) and accessed through the space-free symlink `~/.n64-bootstrap` (libdragon tools are not space-safe, and this repo path contains a space). `setup.sh` creates the symlink; re-run it if the toolchain is missing.

## Cold-start bootstrap

Run the setup script from the repo root:

```sh
./setup.sh
```

On cold start, this verifies and extracts the pinned GCC toolchain, checks out pinned libdragon and tiny3d commits, builds both, and installs Blender plus the asset venv and pinned Fast64 under `~/.n64-bootstrap` (bytes stored in-repo at `.n64-bootstrap`, accessed via a space-free symlink).

If core toolchain files are present, `setup.sh` verifies them and refreshes asset-tool dependencies before exiting.

## Project build

From the repo root:

```sh
./compile-rom.sh
```

This locates the toolchain automatically and runs `make`.

Expected output:

```txt
madeline_cube_rom.z64
```

Clean build artifacts:

```sh
./compile-rom.sh clean
```

## Local smoke test

The host-side test suite does not need the N64 toolchain. `gameplay_smoke`
(jump / dash / pickup / respawn) is wired into the runner alongside the other
host tests:

```sh
./tests/run_host_tests.sh   # or: make test
```

Current smoke coverage:

- grounded jump
- air dash consumption
- collectible pickup
- kill-plane respawn reset

## ROM testing notes

Known local results:

- Ares (snap install) launches the ROM via CLI; verified output:
  `Loaded madeline_cube_rom` / `Vulkan Enabled: using paraLLEl-RDP`
- Ares prints the ROM's USB-serial telemetry (`[profiler]`, `[render-phases]`,
  `[counters]`, `[memory]`) to stdout — this is how the
  baseline/telemetry capture (`tools/capture_baseline.sh`) works.

Ares CLI launch (from the repo root):

```sh
LD_LIBRARY_PATH=/snap/ares-emulator/current/usr/lib/x86_64-linux-gnu \
  /snap/ares-emulator/current/usr/bin/ares --no-file-prompt madeline_cube_rom.z64
```

Notes:

- run the snap binary through its own runtime with `LD_LIBRARY_PATH` pointing at
  the snap's bundled lib dir (`librashader.so` lives there); calling the raw
  binary without it fails with `cannot open shared object file`
- `/snap/ares-emulator/current` is a symlink to the active revision (e.g. `41`),
  so the command survives snap updates
- Ares is GUI-only: it opens the emulator window and stays running; there is no
  run-and-exit CLI mode
- `--no-file-prompt` skips the file-open dialog so the ROM boots immediately
  (needed for the automated telemetry capture)

Recommended validation habit:

- Ares is the sole device emulator for this project. Use it for both quick
  smoke launches and serious validation of modern libdragon/tiny3d behavior.
- For automated telemetry capture (baseline / before-after), use
  `bash tools/capture_baseline.sh`; it launches Ares only when no Ares process
  is already running, polls its stdout, and kills only the process group it
  created. Captures include ROM SHA-256 so runtime evidence identifies exact
  build; interactive boot prints `[build] compiled=...` for manual sessions.
  Do not launch a second Ares beside an interactive session.
- For deterministic physics validation, use
  `bash tools/capture_physics_autoplay.sh [wall_seconds] [label]`. It builds a
  separate autoplay ROM and verifies artifact identity, map/render boot,
  released raw climb shoulders, and route metrics; it refuses a live Ares.
  A lone `[phys-test] result=PASS` is not sufficient evidence.

### Windows host with Debian-mounted ext4 checkout

The current machine runs Ares natively on Windows at
`C:/Users/samue/Documents/ares-v147/ares.exe`. The checkout is accessed through
`\\wsl.localhost\Debian\mnt\wsl\PHYSICALDRIVE0p1\Code\Celeste 64 Demake`;
Debian owns compilation, while Windows Ares reads the ROM through that UNC path.
A missing Linux snap does not mean the Windows emulator is unavailable.

From PowerShell in the checkout, use:

```powershell
# Ordinary telemetry; visible permits deliberate manual controller interaction.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/capture_baseline_windows.ps1 -WallSeconds 120 -Label manual -Visible
# Independent deterministic routes; build in Debian and verify the captured SHA.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/capture_physics_autoplay_windows.ps1 -Profile walljump -WallSeconds 30
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/capture_physics_autoplay_windows.ps1 -Profile mantle -WallSeconds 30
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/capture_physics_autoplay_windows.ps1 -Profile corner -WallSeconds 30
```

Both helpers refuse any existing Windows Ares process. Close the interactive
session before capturing. They terminate only the emulator they launch, default
to a hidden window, retain raw stdout/stderr under `build/`, and record the
full ROM SHA-256 plus boot/map/render/input/physics telemetry. The autoplay
helper applies `verify_physics_capture.py` with the selected profile and exact
artifact hash. Its successful route qualification does not establish human
movement feel. Inspect `[build] compiled=...` against the intended rebuild;
when telemetry changes, rebuild the translation unit that emits the marker.

If Z reads pressed while a trigger is released and releases when squeezed,
check the Ares trigger mapping polarity. A `Lo` mapping can activate at the
rest end of a trigger axis; use the correct `Hi` direction for that device,
then prove `z=0 l=0 r=0` at rest and verify press/release in telemetry.
### Ares controller mapping hygiene (stuck-button runbook)

An older device capture on 2026-08-26 showed N64 **Z** stuck high. On
2026-10-07, Windows Ares testing found a separate polarity error: VirtualPad1
R-Trigger was mapped as `Lo`, so raw Z was high at rest and pressing the
trigger released climb. The binding was corrected from `Lo` to `Hi`, backed up
at `C:/Users/samue/Documents/ares-v147/settings.bml.before-trigger-fix-20261007`.
The final walljump, mantle, and corner captures show `z=l=r=0` at rest. If this
symptom returns, verify raw shoulder telemetry and Ares binding polarity before
changing game-side logic:

1. Open Ares → `Settings` → `Input` → controller for Port 1.
2. Find the mapping feeding N64 **Z** (also check **L** / **R** if symptoms
   point at them). Common offenders: analog-trigger mappings whose rest value
   crosses Ares's pressed threshold, and stick-clicks with bad rest states.
3. Rebind or clear the offender. For the tested Windows R-Trigger mapping,
   `Hi` is the working polarity.
4. Launch the ROM and confirm the ungated `[input] z=0 l=0 r=0 ...` telemetry
   line shows all-zero shoulders at rest BEFORE interpreting any gameplay
   behavior. The `[climb] START` line names the raw bits at every grab frame.

Current ROM control map (N64 controller, via `src/user/gameplay/input/input_system.cpp`):

- left analog stick: move
- `A` (or GameCube `Y`): jump
- `B` (or GameCube `X`): dash
- `Z` / `L` / `R`: climb / grab (shoulders only — face buttons and the GCN
  analog-trigger fields are deliberately not read; spurious analog values
  presented as constant grabbing)
- C-Stick + C-Buttons: camera (orbit + zoom)
- `Start`: pause/fixture debug toggle

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
- The toolchain is stored in-repo (`.n64-bootstrap`, git-ignored) and accessed via the space-free symlink `~/.n64-bootstrap`, so it survives `/tmp` cleanup.
- `setup.sh` handles the full cold-start bootstrap; `compile-rom.sh` locates the toolchain automatically.
- The top-level `Makefile` is intentionally small and only compiles project-owned C++ sources.
- A cold Tiny3D bootstrap also builds its examples, so setup is slower than normal project iteration.
- If `~/.n64-bootstrap` is missing, re-run `./setup.sh` to rebuild the toolchain.

### Code and gameplay

- Keeping gameplay rules engine-light paid off immediately; we had a host smoke test before the first ROM existed.
- The current placeholder collision is intentionally crude: one axis-aligned island and one kill plane.
- Camera, collision, input, and gameplay are still tightly simple on purpose. Avoid polishing abstractions before Milestone 1 exposes real needs.
- The current first-pass movement values are in `docs/movement_spec.md`; tune by feel only after checking behavior in ROM.
- **Target hardware is the N64 with the Expansion Pak (8 MB RDRAM).** `rom_main.cpp` calls `assert_memory_expanded()` at boot, so the ROM fails early with a clear error screen if the pak is absent. The whole-map interconnected renderer's streaming/memory budget assumes the full 8 MB heap. Latest weighted-skin Ares telemetry (`build/baseline-weighted-skin-memory-retry-20261008-131312.txt`) reports mallinfo arena `total=6,403,784`, `used=6,369,896`, `free=33,888` bytes. This supersedes older 2.16/2.29 MB figures for the current ROM; historical samples are not controlled comparisons. Mallinfo free is current arena reserve, not total remaining pak capacity (`sbrk` may grow arena); see `docs/memory_budget.md` before estimating full RDRAM headroom. The 4 MB base-RDRAM target was dropped.
- **Build + render state (2026-10-09 candidate):** 9 hot TUs (`tile_streamer.o`, `textured_room_renderer.o`, `lvl_room_renderer.o`, `open_world_renderer.o`, `camera_controller.o`, `player_movement.o`, `animated_model.o`, `player_hair_renderer.o`, `coll_mesh.o`) build at `-O2`; global build stays `-Os`. Near pass uses conservative 3D AABB culling shared with per-cell DLOD; residency remains bounded at 13. Disconnected coplanar DLOD outlines preserve source polygons rather than filling gaps with a convex hull. Hair keeps OG node simulation/counts but now renders 20-vertex/36-triangle spheres instead of 32/60. Weighted skinning fast-paths the 1,011/2,492 production vertices with one influence; multi-influence math is unchanged. Profiler now reports fixed-step, actor-world, player-physics, near-group-build, and textured-near-draw time plus ticks/frame. Dynamic-solid visibility logs each actor's first sample and later transitions with IDs, bounds, camera, and player position for platform-culling diagnosis. Latest ROM SHA-256: `ff9b1d1017ca471c243c4d2d7e4a9d45591ff7b69913fc2e9234a4a11250bdce`; 142 host tests pass and ROM builds clean. Exact-hash Ares visual/FPS/memory qualification remains open. `distant_world_renderer.cpp`, `gameplay/arena.*`, `tests/runtime_smoke.cpp` remain deleted; compact runtime DLOD uses `DistantTierRenderer` + `dlod_loader`.

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
