# Acceptance scenarios

## Scenario 1: New room owns gameplay truth

GIVEN the project-owned `first-room` authored sources
WHEN the gameplay bake runs
THEN `first-room.lvl` and `first-room.colmesh` are generated without using imported `1-1` geometry, and host checks confirm `PlayerSpawn` is the checkpoint, the climb wall carries `MAT_CLIMBABLE`, and the collectible route exists.

## Scenario 2: Spawn is safe on frame one

GIVEN a fresh load of `first-room`
WHEN the player spawns
THEN a floor query hits beneath the spawn anchor, the player is not overlapping a wall, and no respawn fires before input.

## Scenario 3: Room teaches the intended moves

GIVEN the scripted acceptance fixture
WHEN it exercises the dash gap, climb wall, collectible route, and kill drop
THEN each beat reaches its expected anchor and the kill drop returns the player to the checkpoint.

## Scenario 4: Visual room and collision room agree

GIVEN the paired authored gameplay source and render-anchor sidecar fixture
WHEN the room validators compare named anchors
THEN spawn, dash takeoff, dash landing, climb wall, berry route, and kill drop remain within the chosen tolerance.

## Scenario 5: ROM cutover is real

GIVEN a clean ROM build after Inc 5
WHEN the game boots in Ares
THEN the player appears in `first-room`, the room silhouette is coherent, actor pickups remain visible, and a human can complete all four room beats without invisible walls or fall-throughs.

## Scenario 6: Camera resets after respawn

GIVEN the scripted kill-drop path in `first-room`
WHEN the player falls, respawns, and the next camera frame runs
THEN framing matches a fresh checkpoint reset rather than preserving the obstructed pre-respawn boom.

## Scenario 7: Imported room becomes reference-only

GIVEN the shipping build after Inc 6
WHEN DFS inputs and boot-room references are inspected
THEN no shipping target depends on imported `1-1` artifacts, while OG files remain available for design reference.

## Cross-cutting checks

- Re-run `python3 tests/colmesh_smoke.py filesystem/lvl/first-room.colmesh` after every gameplay-geometry change.
- Rebuild the ROM after N64-facing changes and run the host smoke suite after gameplay changes.
- Before Inc 5, confirm renderer-overhaul Inc 7 is complete so the `.t3dm` room path does not suppress actor rendering.
- Keep generated artifact sizes recorded beside the acceptance run so the room does not silently exceed the existing collision or texture budgets.
