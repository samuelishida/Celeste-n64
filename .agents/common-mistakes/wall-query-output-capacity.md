# Wall-query output capacity

Collision query helpers must never write more hits than caller-provided capacity. `QueryWallsMesh` feeds a fixed `kMaxWallHits` stack array; a scene with more overlapping triangles can overflow that array even when the outer `QueryWalls` loop later truncates copied results. Bound writes inside the helper before storing each hit, and test with more overlapping candidates than the capacity under AddressSanitizer.
