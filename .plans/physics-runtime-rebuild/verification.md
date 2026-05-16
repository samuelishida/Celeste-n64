# Acceptance scenarios

## Scenario 1: Spawn and first landing

GIVEN the graybox start room on emulator and stock 4 MB hardware  
WHEN the scene boots with no player input  
THEN the player starts above the floor, lands on the expected frame window, remains numerically valid, and never falls below the legal floor.

## Scenario 2: Captured reference replay

GIVEN actual C# captures for each committed semantic movement scenario  
WHEN the N64 host replay harness runs the same input scripts  
THEN positions, velocities, states, and named events remain within the approved tolerances for every frame.

## Scenario 3: Source-shaped contacts

GIVEN fixture rooms containing a floor, ceiling, opposing walls, slope, ledge, and moving platform  
WHEN the world query suite runs  
THEN ray hits, wall hits, normals, owner IDs, tie breaks, and platform velocity outputs match the expected source-shaped records.

## Scenario 4: Motor ownership

GIVEN a player frame that collides with floor, ceiling, and wall fixtures  
WHEN the movement phase completes  
THEN only the motor has changed resolved position/contact fields, and no later scene/controller path performs a second collision repair.

## Scenario 5: Wall and climb flow

GIVEN captured source runs for wall jump, climb entry, climb release, corner turn, and ledge hop  
WHEN those scenarios replay against the port  
THEN wall probes, movement states, cooldowns, and exit velocities match the captured events.

## Scenario 6: Surface carry

GIVEN slope, ledge, and moving-platform fixtures  
WHEN the player traverses or leaves them  
THEN slope speed changes, short-drop snap, rider displacement, and stored platform velocity match reference captures.

## Scenario 7: Camera and respawn integration

GIVEN an obstructed camera path and a forced respawn  
WHEN the scene updates  
THEN the camera shortens through the shared query layer, respawn restores a valid grounded/motor state, and no numeric exception is reported on ROM.

## Scenario 8: Hardware signoff

GIVEN the complete graybox route on stock 4 MB hardware  
WHEN a manual validation run covers boot, locomotion, climb, platform, camera, and respawn cases  
THEN profiler counters stay inside the accepted frame/memory budget and the feel review signs off against the captured reference corpus.

## Cross-cutting checks

- Modeled traces are removed or clearly marked non-authoritative before final signoff.
- Every increment keeps the graybox route bootable.
- Every richer collision feature lands with a host test and a ROM-visible diagnostic or acceptance check.
- Any coordinate conversion between source `+Z` up and port `+Y` up is tested at the boundary, not scattered through gameplay code.
