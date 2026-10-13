# Authored face identity must survive collision baking

When matching polygon-level collision semantics, do not infer authored-face
identity from coplanarity or triangle adjacency. Distinct polygons may be
coplanar, while one polygon may become many fan triangles; geometric merging
silently changes candidate counts and tie outcomes.

Carry a stable source-face ID through triangulation, BVH reordering, CMSH
serialization, and endian loading. Use it to collapse fan triangles to one
candidate and make traversal-independent ranking deterministic. Do not claim
that canonical source order reproduces OG `SolidGrid` ordering: OG candidate
order depends on spatial bucket and insertion order, which can differ on exact
ties. Keep that limitation explicit unless query-order parity is separately
proven.
