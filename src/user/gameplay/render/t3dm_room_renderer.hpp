#pragma once

#include <t3d/t3dmodel.h>
#include <cstring>
#include <map>

namespace madeline_cube {

// Renders baked room geometry from a .t3dm model.
// Replaces the flat-face LevelRenderer with smooth-shaded .t3dm rendering.
// Collision and entities remain in .lvl + .colmesh.
class T3dmRoomRenderer {
public:
    T3dmRoomRenderer();
    ~T3dmRoomRenderer();

    // Load the room .t3dm from DFS. Returns true on success.
    bool Load(const char* dfs_path);

    // Free the .t3dm model and cached sprites.
    void Free();

    // Draw the room geometry. Call after t3d_matrix_push for the room transform.
    void Draw();

    // Returns true if a .t3dm model is loaded.
    bool IsLoaded() const { return model_ != nullptr; }

private:
    T3DModel* model_ = nullptr;
    T3DMat4FP* identity_fp_ = nullptr;

    // C-string comparison for const char* map keys (avoids per-frame heap alloc).
    struct CStrLess {
        bool operator()(const char* a, const char* b) const {
            return std::strcmp(a, b) < 0;
        }
    };

    // Cached sprites to avoid reloading per frame.
    // Uses const char* keys to avoid per-frame std::string allocations.
    std::map<const char*, sprite_t*, CStrLess> sprites_;
};

}  // namespace madeline_cube
