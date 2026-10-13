#include "gameplay/render/material_meta_contract.hpp"

#include <cstdio>

int main() {
    using madeline_cube::IsSupportedMaterialMetaFilter;
    if (!IsSupportedMaterialMetaFilter("none", false)) {
        std::fprintf(stderr, "textureless MATMETA slot must accept filter=none\n");
        return 1;
    }
    if (!IsSupportedMaterialMetaFilter("bilinear", true) ||
        !IsSupportedMaterialMetaFilter("nearest", true)) {
        std::fprintf(stderr, "textured MATMETA slots must accept supported filters\n");
        return 1;
    }
    if (IsSupportedMaterialMetaFilter("none", true) ||
        IsSupportedMaterialMetaFilter("bilinear", false) ||
        IsSupportedMaterialMetaFilter("unknown", true)) {
        std::fprintf(stderr, "MATMETA filter/path mismatch must be rejected\n");
        return 1;
    }
    return 0;
}
