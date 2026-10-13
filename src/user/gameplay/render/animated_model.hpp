#pragma once

#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gameplay/math_types.hpp"
#include "gameplay/render/animation_manifest.hpp"
#include "gameplay/render/animation_source_curve.hpp"
#include "gameplay/player/animation_state.hpp"
#include "gameplay/player/player_animation.hpp"
#include "gameplay/render/player_pose_sample_cache.hpp"
#include "gameplay/render/player_animation_source.hpp"
#include "gameplay/render/player_head_anchor.hpp"
#include "gameplay/render/weighted_skinning_math.hpp"

namespace madeline_cube {

class AnimatedModel {
public:
    bool Load(const char* model_path, const char* animmap_path, const char* skin_path,
              const char* animation_curve_path);
    void Free();
    bool SetClip(PlayerAnimState state, bool restart, float rate = 1.0f);
    void Update(float dt);
    void Configure(PlayerAnimation& animation) const;
    void Sample(const PlayerAnimation& animation);
    bool PrepareDraw();
    void UpdateMatrix(const Vec3& pos, float scale, float yaw_rad);
    void UpdateMatrix(const Vec3& pos, const Vec3& scale, float yaw_rad);
    bool HeadPoint(const Vec3& source_local_offset, Vec3& world_point) const;
    void Draw() const;
    bool IsLoaded() const { return model_ != nullptr && current_anim_ >= 0; }
    const char* CurrentClipName() const;

private:
    static constexpr int kMaxClips = AnimationManifest::kMaxClips;
    static const char* ClipName(PlayerAnimState state);
    struct PartBinding {
        T3DObjectPart* part = nullptr;
        size_t vertex_pair_offset = 0;
    };
    bool LoadWeightedSkin(const char* skin_path);
    bool LoadAnimationCurves(const char* animation_curve_path);
    bool BuildWeightedSkinBuffers();
    bool UpdateWeightedSkinVertices(T3DVertPacked* destination);
    T3DModel* model_ = nullptr;
    T3DSkeleton skeleton_ = {};
    T3DSkeleton poses_[kMaxClips] = {};
    bool clip_available_[kMaxClips] = {};
    int current_anim_ = -1;
    AnimationManifest manifest_;
    T3DMat4FP* matrix_fp_ = nullptr;
    T3DMat4 model_matrix_ = {};
    PlayerAnimation legacy_animation_;
    PlayerPoseSampleCache pose_cache_;
    AnimationCurve* animation_curves_ = nullptr;
    AnimationCurveKey* animation_curve_keys_ = nullptr;
    uint16_t animation_curve_count_ = 0;
    uint32_t animation_curve_key_count_ = 0;
    int source_scale_bones_[kPlayerSourceScaleTrackCount] = {};
    int head_bone_index_ = -1;
    WeightedSkinVertex* weighted_vertices_ = nullptr;
    WeightedSkinMatrix* inverse_bind_matrices_ = nullptr;
    WeightedSkinMatrix* joint_skin_matrices_ = nullptr;
    T3DVertPacked** weighted_vertex_buffers_ = nullptr;
    T3DVertPacked* model_vertex_base_ = nullptr;
    std::vector<PartBinding> part_bindings_;
    uint16_t source_vertex_count_ = 0;
    uint16_t model_vertex_count_ = 0;
    uint16_t joint_count_ = 0;
    uint8_t weighted_buffer_count_ = 0;
    uint8_t weighted_buffer_index_ = 0;
    size_t packed_vertex_buffer_bytes_ = 0;
    bool draw_ready_ = false;
};

}  // namespace madeline_cube
