#pragma once

#include <cstring>

namespace madeline_cube {

// Textureless materials are valid reserved slots in MATMETA. They use the
// "none" filter alongside an empty ROM path; textured materials need a real
// sampling mode.
inline bool IsSupportedMaterialMetaFilter(const char* filter,
                                          bool has_texture_path) {
    if (!filter) return false;
    if (!has_texture_path) return std::strcmp(filter, "none") == 0;
    return std::strcmp(filter, "nearest") == 0 ||
           std::strcmp(filter, "bilinear") == 0;
}

}  // namespace madeline_cube
