# Acceptance scenarios

## Scenario 1: Engine path is independently healthy

GIVEN a room-scale tiny3d-native known-good fixture with an expected screenshot or checksum
WHEN it is loaded in the ROM using the new static room renderer
THEN Ares shows an intact textured model with correct lighting and no needles, proving the engine path independent of OG map conversion.

## Scenario 2: Current map defects are observable before runtime

GIVEN `assets/og_converted/maps/1-1.map`
WHEN the host bake audit runs
THEN it reports two-axis brush policy, emitted/rejected geometry, winding, duplicate-vertex, degeneracy, and material-budget results deterministically, writes a host-viewable debug export, and emits one route decision for the room.

## Scenario 3: Gameplay and rendering load independently

GIVEN room `1-1`
WHEN the game loads the room after cutover
THEN gameplay systems read collision/entities from LVL1 `.lvl`, visible static geometry comes from `.t3dm`, and either side can fail with a clear log without corrupting the other.

## Scenario 4: First-room visuals are coherent

GIVEN the accepted first-room render asset and `.t3dm`-validated TMEM-safe material set
WHEN the room is viewed in Ares from the spawn camera and while moving through it
THEN the room presents a stable readable silhouette, at least two distinct materials, no long stray triangles, and no helper/logic brushes rendered by accident.

## Scenario 5: LVL1 compatibility is quarantined

GIVEN the final cutover build
WHEN host tests and ROM boot checks run
THEN no shipping render path depends on LVL1 face fans for static room visuals, LVL1 remains readable for compatibility, and any retained legacy draw path is explicit debug-only behavior.

## Scenario 6: New budget guardrail survives the migration

GIVEN a `.t3dm` room artifact that references an oversized sprite
WHEN render-artifact validation runs
THEN the build fails before bundling and names the offending material, without relying on a legacy `.manifest`.

## Cross-cutting checks

- Run the project host smoke suite after each gameplay-facing increment.
- Rebuild the ROM after every N64-facing increment.
- Validate TMEM safety from the active render artifact before bundling room materials.
- Keep an Ares serial log and screenshot for Scenario 1 and Scenario 4.
- Record first-room model stats, ROM-size delta, and an Ares FPS/VPS snapshot before cutover.
- Before quarantining legacy room rendering, prove the new room artifact can be regenerated from source in a clean tree.
