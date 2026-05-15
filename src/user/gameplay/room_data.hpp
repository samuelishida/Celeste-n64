#pragma once

#include "world.hpp"

namespace madeline_cube {

// Returns a statically defined graybox room for the first Forsaken City area.
// This is compiled into the ROM; later rooms will be loaded from baked data.
const Room& GetForsakenCityStartRoom();

}  // namespace madeline_cube
