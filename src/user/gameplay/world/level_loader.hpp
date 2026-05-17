#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/world/world.hpp"

namespace madeline_cube {

struct LevelFace {
    uint32_t vertex_start;
    uint32_t vertex_count;
    uint16_t material_id;
    Vec3 normal;
};

struct LevelVertex {
    Vec3 pos;
    float u, v;
};

struct LevelGeometry {
    static constexpr int kMaxFaces = 512;
    static constexpr int kMaxVertices = 4096;

    LevelFace faces[kMaxFaces];
    int face_count = 0;

    LevelVertex vertices[kMaxVertices];
    int vertex_count = 0;
};

// Load a baked .lvl file. Populates room (collision + spawns) and geometry
// (faces + vertices for rendering). Path may be "rom:/lvl/1-1.lvl" on device
// or a local filesystem path in host tests.
bool LoadLevel(const char* path, Room& room, LevelGeometry& geometry);

}  // namespace madeline_cube
