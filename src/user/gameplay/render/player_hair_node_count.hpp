#pragma once

namespace madeline_cube {

// OG SetHairColor selects 18 nodes in Feather, otherwise 16 with two or more
// charges and 10 with zero or one charge.
inline int ResolvePlayerHairNodeCount(int dashes, bool in_feather_state = false) {
    if (in_feather_state) return 18;
    return dashes >= 2 ? 16 : 10;
}

}  // namespace madeline_cube
