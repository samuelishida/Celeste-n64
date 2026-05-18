# CollMesh Ownership: Fix Test Leaks

## Summary

`LoadLevel` allocates a `CollMesh*` via `LoadCollMesh` and stores it in `Room::coll_mesh`. `GameplayScene::Shutdown` correctly pairs this with `FreeCollMesh`. Two host tests — `level_loader_test.cpp` and `player_motor_collmesh_test.cpp` — call `LoadLevel` but never call `FreeCollMesh`, leaving the allocation un-freed. This adds the missing teardown to both tests so `valgrind`/sanitizer runs are clean and the ownership contract stated in `coll_mesh.hpp:79` is honored everywhere.

## Files to touch

### tests/level_loader_test.cpp

Add `#include "../src/user/gameplay/physics/coll_mesh.hpp"` and call `physics::FreeCollMesh(room.coll_mesh)` before `return 0`. The test already validates `room.coll_mesh` indirectly (collider_count == 0 implies colmesh path ran); teardown goes right before the final return.

### tests/player_motor_collmesh_test.cpp

`coll_mesh.hpp` is already included at line 6 — no new include needed. `room_ptr` and `geom_ptr` (lines 136–137) are heap-allocated with `new` and also never deleted. At every exit path after a successful load (grounded-agreement fail ~line 260, wall-agreement fail ~line 264, success `return 0` ~line 268) add teardown in order: `physics::FreeCollMesh(room_ptr->coll_mesh); delete geom_ptr; delete room_ptr;`. The early-exit paths at ~lines 141 (LoadLevel fail) and ~145 (coll_mesh null) exit before the mesh is set; those paths already do `return 1` without ownership, which is correct.

## Edge cases

- Early-exit on `LoadLevel` failure (line 141): `coll_mesh` is null, no free needed. Current null check at line 261 of `gameplay_scene.cpp` confirms this pattern.
- Null-check before free: `FreeCollMesh` already guards on null per convention; defensive null check before call is unnecessary.

## Verification

- Build level_loader_test: `g++ -std=c++17 -Isrc/user -fsanitize=address tests/level_loader_test.cpp src/user/gameplay/world/level_loader.cpp src/user/gameplay/world/room_data.cpp src/user/gameplay/physics/coll_mesh.cpp src/user/gameplay/physics/geom.cpp -o /tmp/level_loader_test`
- Build player_motor_collmesh_test (requires fixtures): see `tests/player_motor_collmesh_test.cpp` for full TU list; add `-fsanitize=address`.
- Done when (level_loader_test): GIVEN `filesystem/lvl/1-1.lvl` exists, WHEN `/tmp/level_loader_test` runs, THEN exits 0 with no ASAN leak report.
- Done when (player_motor_collmesh_test): GIVEN `filesystem/lvl/1-1.lvl` and colmesh exist, WHEN the binary runs, THEN exits 0 (`[inc6] PASS`) with no ASAN leak report.

## Decisions and assumptions

- Decision: fix tests only; no change to `LoadLevel` API or Room struct ownership semantics. Source: code @ `src/user/gameplay/scene/gameplay_scene.cpp:261` (GameplayScene already owns and frees correctly — no callers need changing).
- Assumption: `FreeCollMesh` is safe to call with a non-null pointer (confirmed by `coll_mesh.hpp:81` contract and `gameplay_scene.cpp:261` usage).
- Decision: no RAII wrapper introduced. The existing explicit-free pattern in GameplayScene is the convention; adding a wrapper would be a separate architectural change outside this plan's scope.

## Estimated scope

S

## Increments

### Inc 1: Fix level_loader_test.cpp

Add `#include "../src/user/gameplay/physics/coll_mesh.hpp"` at the top. Before `return 0;` at the end, add `if (room.coll_mesh) physics::FreeCollMesh(room.coll_mesh);`.

**Done when:** GIVEN `filesystem/lvl/1-1.lvl` exists, WHEN the test compiles and runs, THEN exits 0 and ASAN reports no leaks.

### Inc 2: Fix player_motor_collmesh_test.cpp

Add teardown at three return points:
1. ~line 260 (grounded-agreement fail): add `physics::FreeCollMesh(room_ptr->coll_mesh); delete geom_ptr; delete room_ptr;` before `return 1;`
2. ~line 264 (wall-agreement fail): add same teardown before `return 1;`
3. ~line 268 (success): add same teardown before `return 0;`

**Done when:** GIVEN `filesystem/lvl/1-1.lvl` and colmesh exist, WHEN the test compiles and runs, THEN exits 0 with `[inc6] PASS` and ASAN reports no leaks.

**Audit checkpoint:** yes (executed)
audit-ckpt1: tier=light specialists=[logic] reason="Small scope (11 lines, 2 test files), no memory-safety or architecture risks. Teardown patterns consistent and use standard cleanup APIs."
  fixes=3 plan-overrides=0 behavior-changes=0 covers=Inc 1, Inc 2

### Audit findings

1. **FIX** [tests/player_motor_collmesh_test.cpp:140-147] — Early returns leak room_ptr/geom_ptr. Added cleanup before both `return 1;` calls.
2. **FIX** [tests/player_motor_collmesh_test.cpp:158-159] — ReadTrace error handling broken (assert on -1). Added check for `n < 0` before asserting.
3. **FIX** [tests/player_motor_collmesh_test.cpp:268] — Comment/guard mismatch (90% vs 75%). Updated comment to match 75% threshold.

All FIXes verified: test compiles, runs, exits 0 with `[inc6] PASS`, ASAN reports no leaks.

## Open questions (CONSIDER from review)

- Consider adding a comment near the top of `player_motor_collmesh_test.cpp` noting that `room_ptr`/`geom_ptr`/`coll_mesh` require manual teardown — five explicit-free call sites across two files is a latent leak source as return paths grow.
