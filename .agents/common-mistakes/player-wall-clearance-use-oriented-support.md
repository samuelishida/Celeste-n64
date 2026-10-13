# Player wall clearance must follow the rendered model orientation

## Symptom

Madeline appears several units away from a wall while climbing even though the
render-only gap is configured to be small, especially after shrinking the
animated model.

## Cause

Using the horizontal radius of the model's bounding-box corners as the wall
support assumes a circular silhouette. For the packed Madeline mesh, that
radius is about 10.84 world units at the current scale, while the actual
front-facing support is about 6.34 units. The excess becomes a visible gap
because the render offset moves the model root far enough to clear the assumed
circle.

## Fix

Keep the conservative radius for nearby-wall queries, but compute the mesh's
wall-plane support from its packed T3DM X/Z bounds after projecting them through
the current facing direction. Retain the configured wall gap as a small margin.

## Guardrail

When changing player scale, keep the runtime scale and feet pivot synchronized
with the model envelope. Do not reuse an isotropic corner radius as the visible
support against a wall; verify that the oriented bound still clears both planes
during corner easing.
