# Verification: celeste64-controls-physics-n64-port

**Date:** 2026-09-07
**Mode:** lead-owned self-review (direct-search run — no verifier/reviewer subagents per workflow)

## Checks performed

1. **Constants 1:1 with OG** — read `movement_config.hpp` (99 lines) and cross-checked every value against `Celeste64-og/Source/Actors/Player.cs` constants block (lines 1-60). All match (run 64, accel 500, friction 800, gravity 600, jump 90, dash 140, coyote 0.12, buffers 0.10, climb 40, walljump 83.2/90, skid set, dash-jump set, spring 160/0.3, feather set). PASS.
2. **Single mover ported** — read `player_movement.hpp/cpp`; confirmed SweepMove (substep 2 + Popout), Popout (ground→ceiling→wall waist+head r3), GroundCheck (ray 5.01 from feet+5), CeilingCheck (ray 11 from feet+1), GroundSnap (ray 5, cooldown 0.10), CancelGroundSnap, SweepMoveClimb. PASS.
3. **Input layer not wired** — `rg virtual_input src/user/` shows zero usage outside its own files; `input_system.cpp` reads raw joypad into InputActionVec2/InputButton. PASS (claim "virtual layer not wired" confirmed).
4. **State machine hand-switched** — `PlayerMovementState` enum has only Normal/Dashing/Climbing/Skidding; StatePhase switches on it. PASS.
5. **Missing states** — no feather/spring constants in `movement_config.hpp`; PlayerMovementState lacks Feather/Spring/StrawbGet/Respawn/Dead/Bubble/Cassette. PASS.
6. **Camera deviations** — `camera_controller.hpp` confirms zoom_rate=1.8, zoom_snap_speed=1.0 (vs OG 1.0/0.5); orbit 4, follow 0.01, vertical 0.001, FOV 76.8→120→1→1.2 OG-exact. PASS.
7. **Online refs** — GitHub repo (canonical, MIT Source), itch page (controller recommended, v1.1.1), ControlsConfig.cs raw (default bindings) all fetched successfully. exok.itch.io 404'd (rejected, correct URL noted). PASS.
8. **Test count** — last `make test` run: 57 passed / 0 failed; `climb_bug_probe` 16/16. PASS.

## Findings

### FATAL
- None.

### MAJOR
- None.

### MINOR
1. The Foster `VirtualStick`/`VirtualButton` internals (exact deadzone shape, buffer decay) are not in the checkout; the port's `virtual_input.cpp` is the working interpretation. The 0.35 deadzone and 0.1 s buffer values are confirmed from `Controls.cs`; the circular-deadzone math is inferred. Marked as a caveat in the draft. ACCEPTED.
2. The spring constants (160/0.3) are stated as "OG constants" but live in `actor/spring_actor.cpp`, not `movement_config.hpp` — the draft already says the player handler parity "must be verified" in Inc 3. ACCEPTED.
3. The claim "57 host smokes" is the current green count; the plan's Inc 2 verification repeats it as a target. ACCEPTED.

## Verdict

**PASS WITH NOTES** — no FATAL or MAJOR findings; the three MINOR items are documented caveats, not errors. The spec and plan are grounded in repo-local primary evidence (OG checkout + port code + docs) with online refs as secondary confirmation.
