# OG single-mover port (og-mover-overhaul)

## Reuse
- Porting OG Celeste64 movement/collision to the demake; single-mover vs multi-path collision; controller/mover restructure.

## Learnings
- **One mover, one repair primitive**: OG Celeste64 has exactly ONE `SweepTestMove` + `Popout` called once per tick. The demake's four parallel repair paths (substep sweeps, sphere floor gates, corner teleport, 50-unit snap) is what caused persistent clip + control bugs that four patch cycles never converged on. Relocating everything onto one swept mover (`MoveService`) + ray ground closed the clipping class **by construction** (every tick ends popped-out).
- **Move-math NOT re-ported**: the feel formulas (approach/rotate/jump/dash) already existed byte-OG-exact in the controller; the plan's real work was relocation + gate change, not rewriting movement math. Don't re-port validated code.
- **OG Popout wall test is a SEPARATE if**, not an else-after-ground/ceiling: a grounded player running into a wall must still be popped out along the wall normal. Chaining the wall test in the else-chain silently disabled wall popout whenever grounded.
- **Feet-anchored coordinate conversion**: OG stores feet position, demake stores CENTER. Every OG Z-offset must convert (waist = center - half_height + 3), and ground pushout must land the FEET (center' = hit + half_height), not the center.
- **Popout returns true only for wall pops** (drives the resolveImpact-once latch); a ground/ceiling repair alone must not trip it.
- **One-way platforms**: pass-through from below means the floor gate accepts when feet AT/above surface (strict `<` = below = pass-through); the ceiling gate rejects one-ways when feet below the surface.
- **Zero-amount SweepMove**: OG still runs a separate `Popout(false)` "for good measure" every tick — the mover must idle-popout on zero movement or contact flags (wall_contact) drop and tests for rest/flicker regress.
- **Skidding is ground-restricted**: entering skid on a fast reverse requires `grounded`; host fixtures must set a real floor plane (not just `grounded=true`) or MovePhase's ray GroundCheck flips grounded false and cancels skid next tick.
- **Host-test migration pattern**: dead motor tests → either delete (pure-motor) or migrate (behavioral) onto `MoveService` + `MovePhase`. Watch for: (a) feet-anchored half_height=10 changes checkpoint y, (b) ground-snap ray is only 5 units (not 50), (c) duplicate link sources in runner cause "multiple definition" — use shared GAMEPLAY_LINK, (d) `Config()` accessor may not exist on controller — use `MoveServiceConfig{}.half_height` or the config struct directly.
- **Run every plan increment's closure gate**: `grep player_motor src/` must return empty after motor deletion — check comments too (update stale `PlayerMotorConfig` comments to `MoveServiceConfig`).
