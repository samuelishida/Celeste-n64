#pragma once

#include <t3d/t3dmodel.h>
#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/render/pass_camera_math.hpp"

namespace madeline_cube {

// Skybox (arch.md §29). Drawn before the distant pass with a rotation-only
// transform (camera-relative translation zeroed) so the sky stays stationary
// relative to the camera while terrain moves. In the first version it is a
// flat-colored dome (no sprite); the API supports an optional sprite later.
//
// Device-only (includes t3d). The host-testable transform contract lives in
// `skybox_math.hpp`.
class Skybox {
public:
    Skybox() = default;
    ~Skybox() { Free(); }
    Skybox(const Skybox&) = delete;
    Skybox& operator=(const Skybox&) = delete;

    // Initialize the skybox. `sprite_path_or_null` is reserved for a future
    // textured dome; passing null uses a flat-colored dome.
    void Init(const char* sprite_path_or_null);

    // Draw the skybox. `cam` is the near camera (used for orientation only;
    // the translation is zeroed per arch.md §29). Call within a
    // t3d_frame_start/end pair, before the distant pass.
    void Draw(const CameraDesc& cam);

    // Free resources.
    void Free();

    bool IsLoaded() const { return verts_ != nullptr; }

private:
    T3DVertPacked* verts_ = nullptr;
    uint32_t pair_count_ = 0;
    T3DMat4FP* matrix_fp_ = nullptr;
};

}  // namespace madeline_cube
