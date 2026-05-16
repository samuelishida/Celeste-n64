# Acceptance scenarios

## Scenario 1: Trace corpus is real evidence

GIVEN the chosen C# reference revision and the host parity tools  
WHEN the trace command runs for the committed scenario set  
THEN it emits deterministic fixtures for jump, dash, skid, wall, climb, platform, and camera cases and fails on any unapproved drift.

## Scenario 2: Swept motion replaces coarse collision

GIVEN a graybox fixture with floor, ceiling, wall, and short-drop geometry  
WHEN the player traverses it at the highest supported locomotion speed  
THEN the motor resolves contacts through sweep/popout/late-ground phases without tunneling, wall penetration, or one-frame false airborne transitions.

## Scenario 3: Core locomotion matches the source target

GIVEN the parity scenarios for run, full jump, short jump, coyote jump, dash, dash jump, and skid  
WHEN the port replays them at fixed 60 Hz  
THEN positions, velocities, state transitions, and exact-event frames remain within the approved tolerance profile for each scenario.

## Scenario 4: Wall and climb behaviour is no longer placeholder logic

GIVEN wall fixtures with straight walls, corners, and a climbable ledge  
WHEN the player performs wall jump, climb enter, climb release, corner turn, and ledge hop scenarios  
THEN each scenario succeeds without relying on jump-held-as-grab behaviour and matches the source trace expectations.

## Scenario 5: Surface-aware movement survives real geometry cases

GIVEN fixtures for a slope, platform edge, short drop, and moving platform  
WHEN the player runs, jumps, leaves a ledge, and takes off from the platform  
THEN slope speed, ledge steering, ground snap, and carried velocity behave as the committed fixtures specify.

## Scenario 6: Camera uses the same world understanding

GIVEN an obstructed camera path, a low ceiling, normal jumping, and a climb case  
WHEN the camera controller updates  
THEN camera shortening, push-down, vertical dead-zone behaviour, and climb framing match the deterministic camera fixtures.

## Scenario 7: Hardware acceptance remains honest

GIVEN the finished movement runtime on emulator and stock 4 MB hardware  
WHEN the full graybox route is played through for the acceptance script and a 10-minute soak  
THEN the route remains playable, trace tests pass, frame time stays within the agreed budget, and memory headroom remains inside the stock-hardware target.

## Cross-cutting checks

- Every increment ends with a host test command and an N64 ROM build that both pass from a clean checkout.
- Query and motor hot paths report enough counters to compare cost before and after the refactor.
- Any intentional trace change updates fixture metadata and the signed-off tolerance profile in the same change.
- The old coarse resolver is removed only after the new motor owns the playable scene and replacement tests cover the behaviour it used to provide.
