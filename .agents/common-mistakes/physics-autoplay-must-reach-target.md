# Physics autoplay must reach target through gameplay

## Symptom

A device autoplay reports successful wall-climb entry even though normal
movement never reached or touched the wall.

## Cause

The scripted harness searches for a valid wall, then directly writes the player
position to its climb anchor before pressing the climb input. This bypasses
approach movement, normal wall collision, facing, and entry distance; passing
climb telemetry cannot validate those paths.

## Fix

Store target point and normal, approach using ordinary movement input, and
attempt the grab only when the live waist-to-plane gap reaches
`ClimbCheckDist`. Gate results on measured approach displacement and actual
entry, then record climb exit (mantle or walljump) and clearance.

## Guardrail

Require a nonzero start-to-entry displacement and an entry gap within
`ClimbCheckDist`; keep scripted input source attribution explicit. A host
fixture may still teleport to isolate a narrow controller case, but it cannot
stand in for the device route. For corner behavior, start at manifest `Start`,
approach a baked wall with ordinary movement, then retain climb input through
the ease; require exactly one corner adoption, arrival on its destination
normal, stable grip ticks, bounded per-tick displacement, and both-plane model
clearance. A wall-jump/mantle pass does not cover corner traversal.
