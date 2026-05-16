#pragma once

#include "world.hpp"

namespace madeline_cube {

// Returns a statically defined graybox room for the first Forsaken City area.
// This is compiled into the ROM; later rooms will be loaded from baked data.
const Room& GetForsakenCityStartRoom();

// Query fixture room used by host-side physics tests.
const Room& GetPhysicsQueryGrayboxRoom();

// Build a fresh room with a single moving platform whose collider is owned
// by a MovingSurface. Caller owns the returned room and may move
// `moving_surfaces[0].position` between AdvanceMovingSurfaces calls.
Room BuildMovingPlatformFixtureRoom();

// Build a room with a flat floor plus a 45-degree slope plane.  The slope
// spans x in [-5, 0], rising from y=0 at x=0 to y=5 at x=-5.  The slope
// normal is (sin45, cos45, 0) -- it faces up and toward +X (downhill is +X).
Room BuildSlopeFixtureRoom();

// Build a room with an upper floor at y=0 spanning x in [-10, 0] and a lower
// floor at y=-1 spanning x in [0, 10].  Used for ground-snap-over-drop and
// no-snap (airborne) ledge cases.
Room BuildLedgeFixtureRoom();

}  // namespace madeline_cube
