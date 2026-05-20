## OG map polygons can bake with reversed winding

### Symptom

The imported `1-1` room renders as giant stretched slabs or visibly collapsed
faces even though collision queries still load and pass.

### Cause

`tools/bake_map.py` sorted clipped brush-face vertices in the opposite winding
from the transformed face normal, and clipping could also leave duplicate
adjacent vertices in the polygon. The renderer fans those vertices directly, so
reversed or duplicated points turn into degenerate or self-crossing triangles.

### Fix

Reverse the `sort_vertices_ccw(...)` result before emitting the face and strip
consecutive duplicate vertices with `dedupe_polygon_vertices(...)` before
writing LVL vertices.

### Guardrail

`python3 tests/level_bake_report_smoke.py` should report:

- `duplicate_vertex_faces=0`
- `first_fan_degenerate_faces=0`
- `reversed_winding_faces=0`
