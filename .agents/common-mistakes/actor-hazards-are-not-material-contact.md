# Actor hazards are not material contact

Do not model OG DeathBlock or SpikeBlock behavior as `MAT_DEATH` collision-face contact. OG tests the player's waist point against each actor's transformed `WorldBounds` before player state and movement updates. SpikeBlock adds a directional gate using normalized full-3D velocity and actor direction. Face contact changes timing, shape, and spike approach behavior.

Keep source actor bounds and transforms in the map pack as hazard volumes. At runtime, evaluate inclusive point-in-AABB at the OG waist anchor before movement; DeathBlock always kills on overlap, while SpikeBlock kills only when `Dot(normalizedVelocity, direction) < 0.5`. Match the static-solid ray direction correction against OG's 10-game-unit limit. Test transformed source bounds, inclusive edges, pre-movement timing, zero velocity, and both sides of the directional threshold.

Do not reuse the lethal SpikeBlock predicate for climbing obstruction. OG `World.Overlaps<SpikeBlock>` checks `WorldBounds.Contains(point)` without velocity/direction filtering. Climb has two different probes and cancels only the blocked up or side component; zero climb velocity must not make an overlapping spike disappear.

For this map, host/runtime proof covers 98 spikes, 5 DeathBlocks, and 35 corrected spike directions. That is not device proof: still capture a rebuilt-ROM hazard route in Ares and verify actor visuals separately.
