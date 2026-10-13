# Distant LOD must not hull disconnected faces

## Symptom

Distant terrain contains phantom slabs, filled gaps, or floating geometry that
does not exist in the source map.

## Cause

Coplanar polygons can form multiple disconnected boundary loops. If outline
union or ear clipping fails, taking a convex hull across all vertices fills
empty space between those loops and invents visible DLOD surfaces.

## Fix

When a coplanar outline cannot be triangulated without changing its covered
region, triangulate the original convex source polygons independently. Never
use a convex hull as a topology fallback. Drop triangles that become
degenerate after output quantization.

## Guardrail

Keep a regression with separated coplanar patches and assert output area equals
source area. Then rebake the map and inspect DLOD transitions in Ares; host
geometry tests cannot prove visual stability or frame-time targets.
