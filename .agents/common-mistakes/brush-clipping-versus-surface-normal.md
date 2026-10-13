# Brush clipping normals must not become surface normals

Parsed Quake brush planes point toward the interior because compute_face_polygon keeps dot(normal,p)+dist>=0. Negate the normal once at the output boundary; retain inward planes for clipping. Sort emitted polygon winding against the outward normal. Both global CMSH and LVL visuals must use it.

Checking that triangle normals match polygon.normal is insufficient: both can be consistently inward. Check the signed dot of every face plane against an independent brush vertex mean, then verify emitted/baked triangles point away from that interior point.

Never fix this by orienting normals toward the query point or ray. Such flips turn tops into floors from either side, accept unreachable wall backs, and mask culling bugs. Backface rejection must happen before nearest-hit selection.

Regression: tests/authored_winding_contract.py checks source faces, actual quantized CMSH triangles, Start top and known pillar side. physics_query_regression checks that a nearer backface does not mask a farther frontface.
