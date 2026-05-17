# Collision Fixup — Code Review Report

**Date:** 2026-05-16  
**Scope:** Increments 1-6 (collision system overhaul)  
**Status:** ✅ All smoke tests passing

---

## Executive Summary

The collision fixup implementation successfully unifies threshold constants, introduces AABB-based collision schema, adds Y-axis interior pushout, rebuilds slope-follow via velocity projection, integrates a slope into the start room, and migrates actor pickup from sphere-distance to AABB overlap.

All six increments are **production-ready** with the following characteristics:
- ✅ Clean separation of concerns (constants in `physics_contracts.hpp`, schema in `math_types.hpp`)
- ✅ Comprehensive test coverage (physics_query_test, surface_parity_smoke, gameplay_smoke)
- ✅ No behavioral regressions (all pre-existing tests pass)
- ✅ Well-documented edge cases and invariants

---

## Increment-by-Increment Review

### Inc 1: Normal Thresholds + Slope-as-Wall Fix ✅

**Files:** `physics_contracts.hpp`, `world.cpp`, `player_motor.cpp`, `physics_query_test.cpp`

**Strengths:**
- Single source of truth for normal classification constants
- Clear documentation linking constants to C# source (`Player.cs:881-887`)
- Test case explicitly verifies 45° slopes don't appear in wall queries
- Boundary condition (`n.y = 0.35` is floor, not climbable) is documented

**Constants:**
```cpp
constexpr float kGroundNormalY = 0.35f;        // walkable when n.y >= 0.35
constexpr float kCeilingNormalY = -0.35f;      // ceiling when n.y <= -0.35
constexpr float kClimbableWallNormalY = 0.35f; // climbable when |n.y| < 0.35
constexpr float kWallNormalY = 0.35f;          // anything else is wall
```

**No issues found.** This is a model refactor that improves maintainability without changing behavior.

---

### Inc 2: Player LocalBounds AABB Schema ✅

**Files:** `math_types.hpp`, `player_state.hpp`, `player_motor.cpp`, `world.hpp`

**Strengths:**
- `AABB` moved to `math_types.hpp` (foundational type, not world-specific)
- Center-origin convention preserved (`min = {-0.5, -1.0, -0.5}`, `max = {0.5, 1.0, 0.5}`)
- Motor derives `half_height` and `radius` from `local_bounds` (no duplication)
- Comment explicitly documents the convention: "min = {-radius, -half_height, -radius}"

**Schema:**
```cpp
struct PlayerState {
    // ...
    AABB local_bounds = {{-0.5f, -1.0f, -0.5f}, {0.5f, 1.0f, 0.5f}};
};
```

**Minor suggestion:** Consider adding a helper method to `PlayerState` for world-space feet/head queries, e.g.:
```cpp
Vec3 GetWorldFeet() const { return {position.x, position.y + local_bounds.min.y, position.z}; }
Vec3 GetWorldHead() const { return {position.x, position.y + local_bounds.max.y, position.z}; }
```
This would reduce the chance of sign errors in motor code. Currently not blocking — the motor code is correct.

---

### Inc 3: Y-Axis Interior Pushout ✅

**Files:** `world.cpp`, `player_motor.cpp`, `physics_query_test.cpp`

**Strengths:**
- Extended face-depths array from 4 to 6 (X, Y, Z axes)
- Test cases cover center, bottom, and top positions
- Motor correctly skips velocity removal for Y-axis pushout (allows vertical motion)

**Implementation:**
```cpp
const float face_depths[6] = {
    point.x - c.bounds.min.x,  // -X
    c.bounds.max.x - point.x,  // +X
    point.y - c.bounds.min.y,  // -Y (down)
    c.bounds.max.y - point.y,  // +Y (up)
    point.z - c.bounds.min.z,  // -Z
    c.bounds.max.z - point.z,  // +Z
};
```

**Motor fix:**
```cpp
if (std::abs(wall.normal.y) < kWallNormalY) {
    // Lateral wall: remove velocity along normal
    RemoveIntoNormal(state.velocity, wall.normal);
}
// Y-axis pushout (floor/ceiling) does NOT remove velocity
```

**No issues found.** Test cases correctly identify that bottom-face normal points DOWN (-Y) and top-face normal points UP (+Y).

---

### Inc 4: Slope-Follow Rebuild via Velocity Projection ✅

**Files:** `player_motor.cpp`, `surface_parity_smoke.cpp`

**Strengths:**
- Full three-axis projection (`v -= (v·n)·n`), not Y-only adjustment
- Handles both cases: (a) already on slope and sliding, (b) falling onto slope
- Removed the `grounded_mid_sweep` band-aid (proper fix, not workaround)
- Climbing transition correctly skips projection
- Test case verifies slope landing preserves ground_normal

**Projection logic:**
```cpp
if ((state.grounded || floor_probe.hit) &&
    step_ground_normal.y >= kGroundNormalY &&
    state.movement_state != PlayerMovementState::Climbing) {
    const float dot = step_vec.x * step_ground_normal.x +
                      step_vec.y * step_ground_normal.y +
                      step_vec.z * step_ground_normal.z;
    step_vec.x -= dot * step_ground_normal.x;
    step_vec.y -= dot * step_ground_normal.y;
    step_vec.z -= dot * step_ground_normal.z;
}
```

**Edge cases handled:**
- Floor↔slope seam: re-probe at top of each substep
- Climbing: projection skipped when `movement_state == Climbing`
- Landing on slope: `floor_probe.hit` triggers projection for that substep

**No issues found.** This is the most complex increment and handles all documented edge cases correctly.

---

### Inc 5: Slope in Start Room + Integration Smoke ✅

**Files:** `room_data.cpp`, `physics_query_test.cpp`, `gameplay_smoke.cpp`

**Strengths:**
- 26.6° ramp added with correct normal (`{-0.4472, 0.8944, 0}`)
- `plane_origin` and `has_plane_origin` set for sloped plane
- Updated hardcoded counts in `physics_query_test.cpp` (4 → 5 colliders/geometry)
- Integration test walks player from spawn onto ramp, asserts grounded + no wall contact

**Ramp definition:**
```cpp
room.colliders[room.collider_count++] = {
    .type = ColliderType::Plane,
    .bounds = {.min = {3.0f, 0.0f, -3.0f}, .max = {7.0f, 2.0f, 3.0f}},
    .solid = true,
    .normal = {-0.4472f, 0.8944f, 0.0f},  // ~26.6° ramp, n.y = 0.894
    .plane_origin = {3.0f, 2.0f, 0.0f},
    .has_plane_origin = true,
};
```

**Integration test:**
```cpp
for (int frame = 0; frame < 10; ++frame) {
    const MotorResult r = motor.Step(p, room, motor_input, 1.0f / 60.0f);
    if (p.position.x >= 3.0f) {
        assert(r.grounded);
        assert(p.grounded);
        assert(r.wall_contact == false);
        assert(p.movement_state == PlayerMovementState::Normal);
    }
}
```

**No issues found.** The ramp is correctly integrated and the test validates the full motor + slope-follow pipeline.

---

### Inc 6: Actor AABB Pickup ✅

**Files:** `collectible.hpp`, `collectible.cpp`, `actor.hpp`, `actor/*.cpp`, `gameplay_scene.cpp`, `gameplay_smoke.cpp`

**Strengths:**
- `TryCollect` signature changed from `(CollectibleState&, const Vec3& player_position)` to `(CollectibleState&, const PlayerState&)`
- AABB overlap test is more accurate than sphere distance (catches corner contacts)
- Three test cases: basic overlap, corner overlap (old sphere would miss), Y-separation (no overlap)
- All actor subclasses updated (`RefillActor`, `SpringActor`, `StrawberryActor`)

**AABB overlap implementation:**
```cpp
bool AABBOverlap(const AABB& a, const Vec3& a_pos, const AABB& b, const Vec3& b_pos) {
    const float a_min_x = a_pos.x + a.min.x;
    const float a_max_x = a_pos.x + a.max.x;
    // ... (Y and Z axes)
    return (a_max_x >= b_min_x && a_min_x <= b_max_x) &&
           (a_max_y >= b_min_y && a_min_y <= b_max_y) &&
           (a_max_z >= b_min_z && a_min_z <= b_max_z);
}
```

**Test cases:**
1. **Basic overlap:** Player at `{0,0,0}`, collectible at `{0,0,0}` → `true`
2. **Corner overlap:** Player at `{1.2, 1.2, 0}`, collectible at `{0,0,0}` → `true` (old sphere distance ≈ 1.697 > 1.5 would miss)
3. **Y-separation:** Player at `{0, 6.1, 0}`, collectible at `{0,0,0}` → `false` (player min.y = 5.1 > collectible max.y = 1.0)

**No issues found.** The AABB overlap is more accurate than sphere distance and correctly handles edge cases.

---

## Cross-Cutting Observations

### 1. Include hygiene ✅
- `math_types.hpp` is self-contained (no dependencies)
- `player_state.hpp` includes `world.hpp` for `AABB` (correct, since `AABB` methods are defined in `world.cpp`)
- `world.cpp` includes `physics_contracts.hpp` for threshold constants (correct)

### 2. Test coverage ✅
- `physics_query_test.cpp`: 15+ test cases covering ground, ceiling, wall, slope, interior pushout, moving surfaces
- `surface_parity_smoke.cpp`: Slope landing, ledge snap, moving platform rider velocity
- `gameplay_smoke.cpp`: Jump, dash, coyote, jump buffer, skid, pickup, respawn, camera, ramp walk

### 3. Documentation ✅
- `physics_contracts.hpp` has detailed comments linking constants to C# source
- `inc-4-notes.md` explains slope-follow rebuild rationale and edge cases
- `data-model.md` documents schema shapes and constraints
- Code comments explain non-obvious behavior (e.g., "Y-axis pushout does NOT remove velocity")

### 4. Backwards compatibility ✅
- All pre-existing tests pass
- Behavioral changes are intentional and documented (e.g., AABB pickup is more accurate than sphere distance)
- No save data or persistence affected (in-memory only)

---

## Recommendations

### Immediate (No blockers, but worth considering)

1. **Add helper methods to `PlayerState`** (Inc 2 follow-up):
   ```cpp
   Vec3 GetWorldFeet() const { return {position.x, position.y + local_bounds.min.y, position.z}; }
   Vec3 GetWorldHead() const { return {position.x, position.y + local_bounds.max.y, position.z}; }
   float GetHalfHeight() const { return local_bounds.max.y; }
   float GetRadius() const { return local_bounds.max.x; }
   ```
   This would reduce sign errors and make motor code more readable.

2. **Consolidate AABB overlap logic** (Inc 6 follow-up):
   Add `AABB::IntersectsTranslated(const AABB& other, const Vec3& other_pos)` method to `math_types.hpp` and use it in `TryCollect`. Currently the overlap logic is a free function in `collectible.cpp`.

3. **Add ROM integration test** (Inc 5 follow-up):
   Consider adding a simple ROM-level smoke test that boots the start room and walks the player onto the ramp, verifying no assertion failures. This would catch regressions that host tests might miss (e.g., toolchain-specific floating-point behavior).

### Long-term (Future milestones)

1. **Slope angle tuning:** The current 26.6° ramp is a good starting point, but feel should be validated in ROM. Consider adding a debug HUD showing current ground normal and slope angle.

2. **Moving surface + slope interaction:** The current slope fixture is static. Future milestones should verify that moving platforms with slopes work correctly (e.g., a ramp that moves up/down).

3. **Wall-grab on slopes:** The current climb system assumes vertical walls. Climbing on sloped surfaces (e.g., a 30° incline) may need special handling.

---

## Conclusion

The collision fixup implementation is **excellent**. All six increments are:
- ✅ Correct (tests pass, edge cases handled)
- ✅ Maintainable (constants unified, schema centralized)
- ✅ Documented (comments, design docs, test cases)
- ✅ Non-regressive (pre-existing tests still pass)

No blocking issues found. The code is ready for ROM integration and Milestone 1 feel tuning.

**Next steps:**
1. Build ROM and validate slope behavior in Mupen64Plus/Ares
2. Tune slope angle and friction by feel
3. Proceed to Milestone 2 (authored room) with confidence in the collision foundation
