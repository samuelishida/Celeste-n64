# Decisions & assumptions

## D1: Reopen the movement milestone around the motor, not the constants

- **Context:** The current controller already mirrors many source constants and states, yet the ROM still feels wrong because it integrates directly and resolves collision only afterward.
- **Decision:** Treat the old movement slice as incomplete and rebuild the runtime around source-like query, sweep, popout, and late-contact phases before further tuning.
- **Consequences:** Near-term code churn increases, but later tuning targets the same physical model the source controller expects.
- **Alternatives rejected:** Continuing to tune the current coarse resolver is faster now but would keep masking missing behaviour with numbers.

## D2: Capture parity evidence before re-porting more states

- **Context:** Existing smoke tests prove broad outcomes, not trajectory timing or feel-critical transitions.
- **Decision:** Commit deterministic source-derived movement traces and scenario tolerances before changing the next layer of controller logic.
- **Consequences:** Behavioural drift becomes reviewable, and later regressions can fail on disk instead of being rediscovered by play feel.
- **Alternatives rejected:** Manual-only tuning is useful for final sign-off but too weak as the sole regression signal.

## D3: Keep gameplay states above an engine-light physics query layer

- **Context:** The source world provides raycasts, wall hits, and surface normals that both movement and camera use; the current port exposes only broad box/plane resolution.
- **Decision:** Add a reusable gameplay-owned query layer and make the player motor consume query results rather than embedding collision guesses inside each state.
- **Consequences:** Normal, climb, platform carry, and camera collision can share one vocabulary while the ROM remains independent of a future editor/runtime stack.
- **Alternatives rejected:** Baking every special case into `PlayerController` would reduce files at first and multiply behaviour bugs later.

## D4: Sequence expressive states after the substrate

- **Context:** Climbing, slope handling, ledge steering, and platform carry all depend on trustworthy normals, pushouts, and ground contact history.
- **Decision:** Land the query layer and source-like motor before re-porting the more expressive movement states.
- **Consequences:** The DAG is less parallel than a feature checklist, but it avoids rewriting the same state logic twice.
- **Alternatives rejected:** Porting climb immediately would create visible progress while depending on placeholder wall probes that are already known to be wrong.

## D5: Preserve the stock 4 MB prototype constraint while increasing fidelity

- **Context:** The original demake plan targets base N64 hardware, and richer physics can quietly turn into a performance dependency.
- **Decision:** Use fixed-capacity runtime structures, avoid hot-loop allocations, and make frame/memory checks part of the final acceptance pass.
- **Consequences:** Some convenience abstractions may stay host-only, but parity work remains honest for the hardware target.
- **Alternatives rejected:** Relaxing to Expansion Pak assumptions would postpone the hardest budget question rather than answer it.

## Assumptions resolved from code

- The source player updates state, sweeps motion, pops out of solids, then refreshes grounded state and ground snap in a later phase. Source: code @ `Celeste64-og/Source/Actors/Player.cs:340`.
- The source movement layer depends on raycasts, wall-hit normals, pushout vectors, ceiling checks, and ground checks rather than a single coarse collision pass. Source: code @ `Celeste64-og/Source/Actors/Player.cs:642`, `Celeste64-og/Source/Actors/Player.cs:2280`, `Celeste64-og/Source/Scenes/World.cs:412`.
- The current port integrates inside `PlayerController::Step` and resolves one broad pass afterward in the scene loop. Source: code @ `src/user/gameplay/player_controller.cpp:437`, `src/user/gameplay/gameplay_scene.cpp:217`, `src/user/gameplay/world.cpp:24`.
- The source controller uses `coyoteZ`, previous velocity, ground normals, ground-snap cooldown, no-move timing, platform velocity storage, dash count, and a dedicated climb state. Source: code @ `Celeste64-og/Source/Actors/Player.cs:395`, `Celeste64-og/Source/Actors/Player.cs:725`, `Celeste64-og/Source/Actors/Player.cs:817`, `Celeste64-og/Source/Actors/Player.cs:1418`.
- The current player state has only the first-pass subset of that bookkeeping. Source: code @ `src/user/gameplay/player_state.hpp:22`.
- Existing gameplay smoke tests assert broad outcomes, not source-parity trajectories. Source: code @ `tests/gameplay_smoke.cpp:1`.
- The prototype still targets stock 4 MB hardware. Source: code @ `.plans/celeste64-n64-demake/decisions.md:D7`.

## Open questions (from review)

- Later imported rooms may require a second plan for baked mesh collision if the graybox query format cannot represent final authored geometry efficiently enough.
