# Decisions & assumptions

## D1: Re-author the first room instead of repairing imported `1-1`

- **Context:** the current import produces a visually broken room and the bake report shows malformed output severe enough that debugging source fidelity would dominate the first authored-room milestone.
- **Decision:** build a clean project-owned first room that preserves the gameplay purpose, not the OG geometry.
- **Consequences:** iteration gets faster and legal/art boundaries stay cleaner, but room shape no longer claims parity with the original.
- **Alternatives rejected:** repairing the imported room first. Its strongest argument is preserving reference layout, but that is the wrong cost center before one good native room exists.

## D2: Pair gameplay and render sources for the first room

- **Context:** gameplay truth and visible geometry already have separate runtime jobs, while the current render path is being split away from LVL1.
- **Decision:** author gameplay in a clean `.map` source and visible geometry in a matching `.glb` source, then keep them aligned with named anchors and acceptance fixtures.
- **Consequences:** collision can stay deliberately simple while visuals evolve, at the cost of drift risk that tests must police.
- **Alternatives rejected:** one source emits everything. That is attractive long-term, but the current map-to-render route is still under active overhaul and should not block the first room.

## D3: Keep this plan content-focused until the renderer foundation is ready

- **Context:** `GameplayScene` already probes for a static `.t3dm` room, but the existing renderer-overhaul plan still owns the full cutover away from the legacy face-stream path.
- **Decision:** allow room brief, gameplay source, render source, and acceptance fixtures to proceed now; require renderer-overhaul cutover before switching the boot room.
- **Consequences:** content work can advance in parallel without creating a second renderer migration.
- **Alternatives rejected:** fold renderer cutover into this plan. That duplicates an active plan and muddies the ownership boundary.

## D4: Do not smuggle new room metadata into this plan

- **Context:** LVL1 currently loads `PlayerSpawn` as both start and checkpoint, and `Room::kill_plane_y` is not authored from `.lvl`.
- **Decision:** the first rebuilt room uses `PlayerSpawn` as its checkpoint and the existing runtime kill plane; authored checkpoint/kill-plane metadata is deferred to a later schema plan if the game needs it.
- **Consequences:** this plan stays content-focused and honest about current runtime capability, but the first room cannot express a distinct checkpoint or room-local kill plane.
- **Alternatives rejected:** extending LVL1 here. That is useful eventually, but it would turn a room rebuild into a schema migration.

## D5: Keep render anchors out of `.t3dm`

- **Context:** the current conversion path is for render meshes, not preserving authoring empties as runtime semantics.
- **Decision:** export render anchors to a validation sidecar fixture; keep `.t3dm` render-only.
- **Consequences:** artifact boundaries stay clean and validators still get the comparison data they need.
- **Alternatives rejected:** requiring `.t3dm` to carry validation anchors. That couples authoring metadata to the shipping render artifact without a present runtime need.

## Assumptions resolved from code

- Milestone 2 explicitly wants a hand-built room with a dash gap, climb wall, collectible route, and respawn challenge. Source: code @ `docs/milestones.md:38-48`.
- Shipping artifacts are already split conceptually into gameplay `.lvl` and render `.t3dm`. Source: code @ `docs/room_artifact_contract.md:1-36`.
- Static collision now loads from a `.colmesh` sidecar next to `.lvl`. Source: code @ `src/user/gameplay/world/level_loader.cpp:81-104`, `src/user/gameplay/world/level_loader.cpp:178-180`.
- LVL1 currently treats `PlayerSpawn` as both start and checkpoint, and does not load authored kill-plane metadata. Source: code @ `src/user/gameplay/world/level_loader.cpp:145-165`, `src/user/gameplay/world/world.hpp`.
- `.colmesh` climbability is driven by material suffixes such as `_climbable`. Source: code @ `tools/colmesh_bake.py`.
- The current map baker still processes brush geometry from all entities, so class filtering is required before trusting a new room bake. Source: code @ `tools/bake_map.py:440-495`.
- The current shipping build still bakes imported `1-1` from `assets/og_converted/maps/1-1.map`. Source: code @ `Makefile:35-63`.
- Runtime already attempts to load `rom:/lvl/1-1.t3dm`, but the existing scene path is still mid-migration. Source: code @ `src/user/gameplay/scene/gameplay_scene.cpp:207-225`, `src/user/gameplay/scene/gameplay_scene.cpp:406-423`.
- Re-authoring over source fidelity is user-confirmed.

## Open questions (from review)

- `first-room` is clear now, but a later naming pass may prefer an ordered id such as `start-room` before more rooms exist.
