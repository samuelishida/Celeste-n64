# Acceptance scenarios

## Scenario 1: buffered input behaves like source

GIVEN jump is tapped one frame before landing
WHEN the player lands while the buffer is still live
THEN exactly one jump fires, and a second state check in the same frame cannot consume the press again.

## Scenario 2: actor lifecycle stays stable during mutation

GIVEN one actor requests another actor and destroys itself during update
WHEN the world resolves changes
THEN typed queries see the old actor until destruction resolves, the new actor only after `Added()`, and iteration never invalidates.

## Scenario 3: source-shaped wall queries remain useful to movement

GIVEN a player waist probe near two wall faces
WHEN nearest-wall and closest-to-normal queries run
THEN they return deterministic face/owner identities and the expected normal for wall-jump / climb code.

## Scenario 4: moving solids carry riders

GIVEN a rider is standing on a moving solid
WHEN the solid moves this tick and the rider jumps next tick
THEN rider displacement matches the solid movement and stored platform velocity is added once with the source-style clamp.

## Scenario 5: frame order stays explicit

GIVEN a moving solid and a rider in one scene tick
WHEN the frame executes
THEN moving-solid displacement is visible to the motor before actor reactions run, and deferred add/remove resolves only after the actor reaction phase.

## Scenario 6: player-specific parity survives the migration

GIVEN the new substrate is active
WHEN the trace suite replays buffered jump, wall snap-up, climb cornering, ledge assist, and moving-platform jump captures
THEN each trace stays within the agreed tolerance against the OG reference captures.

## Cross-cutting checks

- After every increment: run the host smoke suite listed in `AGENTS.md` plus the newly added tests for that increment.
- After Inc 2 and Inc 7: replay the movement trace fixtures in `tests/movement_traces/`.
- After Inc 6: launch the ROM and walk the ramp, climb wall, spring, refill, and strawberry path once; the compatibility layer is not done if the prototype regresses.
- Rollback drill after each increment: revert only that increment and confirm the prior smoke set still passes.
