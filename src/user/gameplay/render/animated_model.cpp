#include "gameplay/render/animated_model.hpp"
#include "gameplay/render/animation_pose_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dskeleton.h>

namespace madeline_cube {

namespace {
constexpr size_t kWeightedSkinHeaderBytes = 16;
constexpr size_t kWeightedSkinRecordBytes = sizeof(WeightedSkinVertex);
constexpr size_t kAnimationCurveHeaderBytes = 16;
constexpr size_t kAnimationCurveRecordBytes = 8;
constexpr uint16_t kMaxAnimationCurves = 512;

uint16_t ReadBE16(const uint8_t* bytes) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

uint32_t ReadBE32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
}

float ReadBEFloat(const uint8_t* bytes) {
    const uint32_t bits = ReadBE32(bytes);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

}

const char* AnimatedModel::ClipName(PlayerAnimState state) {
    switch (state) {
        case PlayerAnimState::Run: return "Run";
        case PlayerAnimState::Jump: return "Jump";
        case PlayerAnimState::Dash: return "Dash";
        case PlayerAnimState::ClimbIdle: return "Climb.Idle";
        case PlayerAnimState::ClimbUp: return "Climb.Up";
        case PlayerAnimState::Skid: return "Skid";
        case PlayerAnimState::Idle: default: return "Idle";
    }
}

bool AnimatedModel::Load(const char* model_path, const char* animmap_path, const char* skin_path,
                         const char* animation_curve_path) {
    Free();
    int fd = open(model_path, O_RDONLY);
    if (fd < 0 || close(fd) != 0) {
        debugf("[animated-model] model missing: %s\n", model_path);
        return false;
    }
    if (!manifest_.Load(animmap_path)) return false;
    model_ = t3d_model_load(model_path);
    if (!model_) return false;
    const uint32_t buffers = display_get_num_buffers();
    skeleton_.skeletonRef = t3d_model_get_skeleton(model_);
    if (!skeleton_.skeletonRef) { Free(); return false; }
    skeleton_.bufferCount = buffers > 0 ? buffers : 1;
    skeleton_.bones = static_cast<T3DBone*>(malloc(sizeof(T3DBone) * skeleton_.skeletonRef->boneCount));
    skeleton_.boneMatricesFP = static_cast<T3DMat4FP*>(malloc_uncached(
        sizeof(T3DMat4FP) * skeleton_.skeletonRef->boneCount * skeleton_.bufferCount));
    if (!skeleton_.boneMatricesFP || !skeleton_.bones) {
        debugf("[animated-model] skeleton allocation FAILED\n");
        Free();
        return false;
    }
    t3d_skeleton_reset(&skeleton_);
    head_bone_index_ = t3d_skeleton_find_bone(&skeleton_, "Head");
    (void)skin_path;  // WSK stays packaged for later qualification; runtime uses Tiny3D joint skinning.
    if (head_bone_index_ < 0 || !LoadAnimationCurves(animation_curve_path)) {
        debugf("[animated-model] animated asset init FAILED (curves=%s)\n",
               animation_curve_path);
        Free();
        return false;
    }
    for (int i = 0; i < manifest_.Count(); ++i) {
        const AnimationClipRecord& clip = manifest_.At(i);
        T3DChunkAnim* definition = t3d_model_get_animation(model_, clip.name);
        if (!definition || !definition->filePath || strcmp(definition->filePath, clip.rom_stream_path) != 0) {
            debugf("[animated-model] stream mismatch clip=%s expected=%s\n", clip.name, clip.rom_stream_path);
            Free();
            return false;
        }
        poses_[i].skeletonRef = skeleton_.skeletonRef;
        poses_[i].bones = static_cast<T3DBone*>(malloc(sizeof(T3DBone) * skeleton_.skeletonRef->boneCount));
        if (!poses_[i].bones) { Free(); return false; }
        t3d_skeleton_reset(&poses_[i]);
        // PAS1 curves are the authoritative playback data. Keep T3DM clip
        // metadata validation above, but do not open its quantized .sdata stream.
        clip_available_[i] = true;
    }
    for (int track = 0; track < kPlayerSourceScaleTrackCount; ++track) {
        source_scale_bones_[track] = t3d_skeleton_find_bone(&skeleton_, kPlayerSourceScaleTracks[track].bone);
        if (source_scale_bones_[track] < 0) {
            debugf("[animated-model] source STEP bone missing: %s\n", kPlayerSourceScaleTracks[track].bone);
            Free(); return false;
        }
    }
    for (int state = 0; state < PlayerAnimation::kClipCount; ++state) {
        auto* definition = t3d_model_get_animation(model_, ClipName(static_cast<PlayerAnimState>(state)));
        if (!definition || std::fabs(definition->duration - kPlayerSourceClipDurations[state]) > 0.0001f) {
            debugf("[animated-model] source duration mismatch: %s; rebuild prepared player assets\n",
                   ClipName(static_cast<PlayerAnimState>(state)));
            Free(); return false;
        }
    }
    matrix_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    if (!matrix_fp_) { Free(); return false; }
    legacy_animation_ = PlayerAnimation{};
    Configure(legacy_animation_);
    if (!SetClip(PlayerAnimState::Idle, true)) { Free(); return false; }
    Sample(legacy_animation_);
    debugf("[animated-model] loaded %s (%d clips, Tiny3D joint skin, exact curves %u/%u)\n",
           model_path, manifest_.Count(),
           static_cast<unsigned int>(animation_curve_count_),
           static_cast<unsigned int>(animation_curve_key_count_));
    return true;
}

bool AnimatedModel::LoadWeightedSkin(const char* skin_path) {
    if (!model_ || !skeleton_.skeletonRef || !skin_path) return false;
    int file_size = 0;
    auto* bytes = static_cast<const uint8_t*>(asset_load(skin_path, &file_size));
    if (!bytes) {
        debugf("[animated-model] weighted skin missing: %s\n", skin_path);
        return false;
    }
    bool valid = false;
    if (file_size < static_cast<int>(kWeightedSkinHeaderBytes) || memcmp(bytes, "WSK1", 4) != 0) {
        goto cleanup;
    }
    {
        const uint16_t version = ReadBE16(bytes + 4);
        const uint16_t joints = ReadBE16(bytes + 6);
        const uint16_t vertices = ReadBE16(bytes + 8);
        const uint16_t record_bytes = ReadBE16(bytes + 10);
        const float base_scale = ReadBEFloat(bytes + 12);
        const uint32_t expected_size = static_cast<uint32_t>(kWeightedSkinHeaderBytes) +
            static_cast<uint32_t>(joints) * 64u + static_cast<uint32_t>(vertices) * record_bytes;
        if (version != 1 || joints == 0 || joints > 256 ||
            joints != skeleton_.skeletonRef->boneCount || vertices == 0 ||
            record_bytes != kWeightedSkinRecordBytes || expected_size != static_cast<uint32_t>(file_size) ||
            !std::isfinite(base_scale) || std::fabs(base_scale - 64.0f) > 1e-5f) {
            goto cleanup;
        }
        joint_count_ = joints;
        source_vertex_count_ = vertices;
        inverse_bind_matrices_ = static_cast<WeightedSkinMatrix*>(malloc(sizeof(WeightedSkinMatrix) * joints));
        joint_skin_matrices_ = static_cast<WeightedSkinMatrix*>(malloc(sizeof(WeightedSkinMatrix) * joints));
        weighted_vertices_ = static_cast<WeightedSkinVertex*>(malloc(sizeof(WeightedSkinVertex) * vertices));
        if (!inverse_bind_matrices_ || !joint_skin_matrices_ || !weighted_vertices_) goto cleanup;

        const uint8_t* cursor = bytes + kWeightedSkinHeaderBytes;
        for (uint16_t joint = 0; joint < joints; ++joint) {
            for (int element = 0; element < 16; ++element) {
                inverse_bind_matrices_[joint].value[element] = ReadBEFloat(cursor);
                if (!std::isfinite(inverse_bind_matrices_[joint].value[element])) goto cleanup;
                cursor += 4;
            }
        }
        for (uint16_t vertex_index = 0; vertex_index < vertices; ++vertex_index) {
            WeightedSkinVertex& vertex = weighted_vertices_[vertex_index];
            for (float& value : vertex.position) {
                value = ReadBEFloat(cursor); cursor += 4;
                if (!std::isfinite(value)) goto cleanup;
            }
            for (float& value : vertex.normal) {
                value = ReadBEFloat(cursor); cursor += 4;
                if (!std::isfinite(value)) goto cleanup;
            }
            memcpy(vertex.joints, cursor, sizeof(vertex.joints));
            cursor += sizeof(vertex.joints);
            float weight_sum = 0.0f;
            bool zero_weight_seen = false;
            for (float& value : vertex.weights) {
                value = ReadBEFloat(cursor); cursor += 4;
                if (!std::isfinite(value) || value < 0.0f) goto cleanup;
                if (value == 0.0f) zero_weight_seen = true;
                else if (zero_weight_seen) goto cleanup;
                weight_sum += value;
            }
            if (std::fabs(weight_sum - 1.0f) > 2e-5f) goto cleanup;
            for (uint8_t joint : vertex.joints) if (joint >= joints) goto cleanup;
        }
    }
    valid = true;

cleanup:
    free(const_cast<uint8_t*>(bytes));
    if (!valid) return false;
    return true;
}

bool AnimatedModel::LoadAnimationCurves(const char* animation_curve_path) {
    if (!model_ || !skeleton_.skeletonRef || !animation_curve_path) return false;
    int file_size = 0;
    auto* bytes = static_cast<const uint8_t*>(asset_load(animation_curve_path, &file_size));
    if (!bytes) {
        debugf("[animated-model] source curves missing: %s\n", animation_curve_path);
        return false;
    }
    bool valid = false;
    if (file_size < static_cast<int>(kAnimationCurveHeaderBytes) || memcmp(bytes, "PAS1", 4) != 0) {
        goto cleanup;
    }
    {
        const uint16_t version = ReadBE16(bytes + 4);
        const uint16_t clips = ReadBE16(bytes + 6);
        const uint16_t bones = ReadBE16(bytes + 8);
        const uint16_t tracks = ReadBE16(bytes + 10);
        const float base_scale = ReadBEFloat(bytes + 12);
        if (version != 1 || clips != PlayerAnimation::kClipCount ||
            bones != skeleton_.skeletonRef->boneCount || tracks == 0 || tracks > kMaxAnimationCurves ||
            !std::isfinite(base_scale) || std::fabs(base_scale - 64.0f) > 1e-5f) {
            goto cleanup;
        }
        auto* descriptors = static_cast<AnimationCurve*>(calloc(tracks, sizeof(AnimationCurve)));
        if (!descriptors) goto cleanup;
        uint32_t total_keys = 0;
        size_t cursor = kAnimationCurveHeaderBytes;
        uint8_t clips_seen = 0;
        for (uint16_t index = 0; index < tracks; ++index) {
            if (cursor + kAnimationCurveRecordBytes > static_cast<size_t>(file_size)) {
                free(descriptors);
                goto cleanup;
            }
            const uint8_t clip = bytes[cursor];
            const uint8_t bone = bytes[cursor + 1];
            const uint8_t path = bytes[cursor + 2];
            const uint8_t interpolation = bytes[cursor + 3];
            const uint16_t key_count = ReadBE16(bytes + cursor + 4);
            const uint16_t reserved = ReadBE16(bytes + cursor + 6);
            const uint8_t components = path == static_cast<uint8_t>(AnimationCurvePath::Rotation) ? 4 : 3;
            const size_t key_bytes = static_cast<size_t>(key_count) * (1u + components) * 4u;
            if (clip >= clips || bone >= bones || path > static_cast<uint8_t>(AnimationCurvePath::Scale) ||
                interpolation > static_cast<uint8_t>(AnimationCurveInterpolation::Step) ||
                key_count == 0 || reserved != 0 || cursor + kAnimationCurveRecordBytes + key_bytes >
                    static_cast<size_t>(file_size) || total_keys > UINT32_MAX - key_count) {
                free(descriptors);
                goto cleanup;
            }
            for (uint16_t previous = 0; previous < index; ++previous) {
                const auto& other = descriptors[previous];
                if (other.clip_index == clip && other.bone_index == bone && other.path ==
                    static_cast<AnimationCurvePath>(path)) {
                    free(descriptors);
                    goto cleanup;
                }
            }
            descriptors[index] = {
                total_keys, key_count, clip, bone, static_cast<AnimationCurvePath>(path),
                static_cast<AnimationCurveInterpolation>(interpolation)
            };
            total_keys += key_count;
            clips_seen |= static_cast<uint8_t>(1u << clip);
            cursor += kAnimationCurveRecordBytes + key_bytes;
        }
        const uint8_t expected_clip_mask = static_cast<uint8_t>((1u << clips) - 1u);
        if (cursor != static_cast<size_t>(file_size) || clips_seen != expected_clip_mask) {
            free(descriptors);
            goto cleanup;
        }
        auto* keys = static_cast<AnimationCurveKey*>(calloc(total_keys, sizeof(AnimationCurveKey)));
        if (!keys) {
            free(descriptors);
            goto cleanup;
        }
        cursor = kAnimationCurveHeaderBytes;
        for (uint16_t index = 0; index < tracks; ++index) {
            const auto& curve = descriptors[index];
            const uint8_t components = curve.path == AnimationCurvePath::Rotation ? 4 : 3;
            cursor += kAnimationCurveRecordBytes;
            float previous_time = -1.0f;
            for (uint16_t key = 0; key < curve.key_count; ++key) {
                AnimationCurveKey& output = keys[curve.key_offset + key];
                output.time = ReadBEFloat(bytes + cursor);
                cursor += 4;
                if (!std::isfinite(output.time) || output.time < 0.0f || output.time <= previous_time) {
                    free(keys);
                    free(descriptors);
                    goto cleanup;
                }
                previous_time = output.time;
                for (uint8_t component = 0; component < components; ++component) {
                    output.value[component] = ReadBEFloat(bytes + cursor);
                    cursor += 4;
                    if (!std::isfinite(output.value[component])) {
                        free(keys);
                        free(descriptors);
                        goto cleanup;
                    }
                }
            }
        }
        animation_curves_ = descriptors;
        animation_curve_keys_ = keys;
        animation_curve_count_ = tracks;
        animation_curve_key_count_ = total_keys;
        valid = true;
    }
cleanup:
    free(const_cast<uint8_t*>(bytes));
    if (!valid) {
        debugf("[animated-model] invalid source curves: %s\n", animation_curve_path);
        free(animation_curves_); animation_curves_ = nullptr;
        free(animation_curve_keys_); animation_curve_keys_ = nullptr;
        animation_curve_count_ = 0;
        animation_curve_key_count_ = 0;
    }
    return valid;
}

bool AnimatedModel::BuildWeightedSkinBuffers() {
    if (!model_ || !weighted_vertices_ || !source_vertex_count_ || !joint_count_) return false;
    model_vertex_count_ = model_->totalVertCount;
    if (model_vertex_count_ == 0 || (model_vertex_count_ & 1u) != 0) return false;
    model_vertex_base_ = t3d_model_get_vertices(model_);
    if (!model_vertex_base_) return false;

    std::vector<uint8_t> seen(source_vertex_count_, 0);
    for (uint16_t index = 0; index < model_vertex_count_; ++index) {
        const int16_t source_id = t3d_vertbuffer_get_uv(model_vertex_base_, index)[0];
        if (source_id < 0 || source_id >= source_vertex_count_) return false;
        seen[static_cast<size_t>(source_id)] = 1;
    }
    if (std::find(seen.begin(), seen.end(), 0) != seen.end()) return false;

    T3DModelIter iter = t3d_model_iter_create(model_, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        for (uint16_t part_index = 0; part_index < iter.object->numParts; ++part_index) {
            T3DObjectPart* part = &iter.object->parts[part_index];
            const uintptr_t base = reinterpret_cast<uintptr_t>(model_vertex_base_);
            const uintptr_t address = reinterpret_cast<uintptr_t>(part->vert);
            if (address < base || (address - base) % sizeof(T3DVertPacked) != 0) return false;
            const size_t pair_offset = (address - base) / sizeof(T3DVertPacked);
            const size_t first_vertex = pair_offset * 2;
            if (first_vertex + part->vertLoadCount > model_vertex_count_) return false;
            part_bindings_.push_back({part, pair_offset});
            // CPU has already applied the full four-weight matrix sum. Avoid
            // Tiny3D applying its legacy one-bone matrix a second time.
            part->matrixIdx = 0xFFFF;
        }
    }
    if (part_bindings_.empty()) return false;

    weighted_buffer_count_ = skeleton_.bufferCount > 0 ? skeleton_.bufferCount : 1;
    weighted_vertex_buffers_ = static_cast<T3DVertPacked**>(
        calloc(weighted_buffer_count_, sizeof(T3DVertPacked*)));
    if (!weighted_vertex_buffers_) return false;
    packed_vertex_buffer_bytes_ = sizeof(T3DVertPacked) * (model_vertex_count_ / 2);
    for (uint8_t slot = 0; slot < weighted_buffer_count_; ++slot) {
        weighted_vertex_buffers_[slot] = static_cast<T3DVertPacked*>(malloc(packed_vertex_buffer_bytes_));
        if (!weighted_vertex_buffers_[slot]) return false;
        memcpy(weighted_vertex_buffers_[slot], model_vertex_base_, packed_vertex_buffer_bytes_);
        // OG Default.glsl does not skin normals. Convert to its actor-local
        // normal basis once; only positions change as animation advances.
        for (uint16_t index = 0; index < model_vertex_count_; ++index) {
            const int16_t source_id = t3d_vertbuffer_get_uv(model_vertex_base_, index)[0];
            if (source_id < 0 || source_id >= source_vertex_count_) return false;
            const WeightedSkinVertex& source = weighted_vertices_[source_id];
            T3DVec3 normal = {{source.normal[0], source.normal[1], source.normal[2]}};
            t3d_vec3_norm(&normal);
            *t3d_vertbuffer_get_norm(weighted_vertex_buffers_[slot], index) = t3d_vert_pack_normal(&normal);
        }
    }
    weighted_buffer_index_ = static_cast<uint8_t>(weighted_buffer_count_ - 1);
    return true;
}

bool AnimatedModel::UpdateWeightedSkinVertices(T3DVertPacked* destination) {
    // Hot loop: sidecar validation guarantees zero-weight lanes trail active lanes.
    if (!destination || !model_vertex_base_ || !weighted_vertices_) return false;
    for (uint16_t joint = 0; joint < joint_count_; ++joint) {
        WeightedSkinMatrix global{};
        memcpy(global.value, &skeleton_.bones[joint].matrix, sizeof(global.value));
        MultiplySkinMatrices(joint_skin_matrices_[joint], global, inverse_bind_matrices_[joint]);
    }

    for (uint16_t index = 0; index < model_vertex_count_; ++index) {
        const int16_t source_id = t3d_vertbuffer_get_uv(model_vertex_base_, index)[0];
        if (source_id < 0 || source_id >= source_vertex_count_) return false;
        const WeightedSkinVertex& source = weighted_vertices_[source_id];
        float position[3]{};
        if (!EvaluateWeightedSkinPosition(source, joint_skin_matrices_, joint_count_, position)) return false;
        int16_t* packed_position = t3d_vertbuffer_get_pos(destination, index);
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(position[axis])) return false;
            if (position[axis] <= -32768.5f || position[axis] >= 32767.5f) return false;
            const float biased = position[axis] + (position[axis] >= 0.0f ? 0.5f : -0.5f);
            packed_position[axis] = static_cast<int16_t>(static_cast<int32_t>(biased));
        }
    }
    data_cache_hit_writeback(destination, packed_vertex_buffer_bytes_);
    return true;
}

void AnimatedModel::Free() {
    pose_cache_.Clear();
    for (int i = 0; i < kMaxClips; ++i) {
        clip_available_[i] = false;
        t3d_skeleton_destroy(&poses_[i]);
    }
    if (model_) {
        for (const PartBinding& binding : part_bindings_) {
            if (model_vertex_base_) binding.part->vert = model_vertex_base_ + binding.vertex_pair_offset;
        }
        t3d_skeleton_destroy(&skeleton_);
        skeleton_ = {};
        t3d_model_free(model_);
        model_ = nullptr;
    }
    if (weighted_vertex_buffers_) {
        for (uint8_t slot = 0; slot < weighted_buffer_count_; ++slot) free(weighted_vertex_buffers_[slot]);
        free(weighted_vertex_buffers_);
        weighted_vertex_buffers_ = nullptr;
    }
    free(weighted_vertices_); weighted_vertices_ = nullptr;
    free(inverse_bind_matrices_); inverse_bind_matrices_ = nullptr;
    free(joint_skin_matrices_); joint_skin_matrices_ = nullptr;
    free(animation_curves_); animation_curves_ = nullptr;
    free(animation_curve_keys_); animation_curve_keys_ = nullptr;
    animation_curve_count_ = 0;
    animation_curve_key_count_ = 0;
    part_bindings_.clear();
    model_vertex_base_ = nullptr;
    source_vertex_count_ = 0;
    model_vertex_count_ = 0;
    joint_count_ = 0;
    weighted_buffer_count_ = 0;
    weighted_buffer_index_ = 0;
    packed_vertex_buffer_bytes_ = 0;
    draw_ready_ = false;
    head_bone_index_ = -1;
    t3d_mat4_identity(&model_matrix_);
    if (matrix_fp_) {
        free_uncached(matrix_fp_);
        matrix_fp_ = nullptr;
    }
    current_anim_ = -1;
}

bool AnimatedModel::SetClip(PlayerAnimState state, bool restart, float rate) {
    const char* name = ClipName(state);
    const AnimationClipRecord* clip = manifest_.Find(name);
    if (!clip) return false;
    int index = -1;
    for (int i = 0; i < manifest_.Count(); ++i) {
        if (strcmp(manifest_.At(i).name, name) == 0) { index = i; break; }
    }
    if (index < 0 || !clip_available_[index]) return false;
    legacy_animation_.rate = std::isfinite(rate) ? std::fmax(rate, 0.0f) : 1.0f;
    legacy_animation_.Play(state, restart);
    current_anim_ = index;
    return true;
}

void AnimatedModel::Update(float dt) {
    if (!IsLoaded() || current_anim_ < 0) return;
    legacy_animation_.Advance(dt);
    Sample(legacy_animation_);
}

void AnimatedModel::Configure(PlayerAnimation& animation) const {
    for (int state = 0; state < PlayerAnimation::kClipCount; ++state) {
        const char* name = ClipName(static_cast<PlayerAnimState>(state));
        for (int i = 0; i < manifest_.Count(); ++i) {
            if (clip_available_[i] && strcmp(manifest_.At(i).name, name) == 0) {
                animation.SetDuration(static_cast<PlayerAnimState>(state),
                    kPlayerSourceClipDurations[state], state != static_cast<int>(PlayerAnimState::Dash));
            }
        }
    }
}

void AnimatedModel::Sample(const PlayerAnimation& animation) {
    if (!model_ || !skeleton_.bones) return;
    PlayerAnimation::SampleTrack samples[PlayerAnimation::kClipCount];
    const int count = animation.Samples(samples, PlayerAnimation::kClipCount);
    if (pose_cache_.Matches(samples, count)) {
        // Propagate changed matrices through every buffered slot before their
        // dirty flags settle. Never bypass Tiny3D buffer bookkeeping.
        t3d_skeleton_update(&skeleton_);
        return;
    }
    const T3DSkeleton* inputs[PlayerAnimation::kClipCount];
    float weights[PlayerAnimation::kClipCount];
    int sampled = 0;
    for (int sample = 0; sample < count; ++sample) {
        int index = -1;
        for (int i = 0; i < manifest_.Count(); ++i) {
            if (clip_available_[i] && strcmp(manifest_.At(i).name, ClipName(samples[sample].state)) == 0) {
                index = i;
                break;
            }
        }
        if (index < 0) continue;
        t3d_skeleton_reset(&poses_[index]);
        for (uint16_t curve_index = 0; curve_index < animation_curve_count_; ++curve_index) {
            const AnimationCurve& curve = animation_curves_[curve_index];
            if (curve.clip_index != static_cast<uint8_t>(samples[sample].state)) continue;
            float value[4];
            if (!SampleAnimationCurve(curve, animation_curve_keys_, samples[sample].time, value)) continue;
            auto& bone = poses_[index].bones[curve.bone_index];
            if (curve.path == AnimationCurvePath::Translation) {
                for (int axis = 0; axis < 3; ++axis) bone.position.v[axis] = value[axis] * 64.0f;
            } else if (curve.path == AnimationCurvePath::Scale) {
                for (int axis = 0; axis < 3; ++axis) bone.scale.v[axis] = value[axis];
            } else {
                for (int axis = 0; axis < 4; ++axis) bone.rotation.v[axis] = value[axis];
            }
            bone.hasChanged = 1;
        }
        for (int track = 0; track < kPlayerSourceScaleTrackCount; ++track) {
            if (kPlayerSourceScaleTracks[track].clip != samples[sample].state) continue;
            const auto& key = SamplePlayerSourceScale(track, samples[sample].time);
            auto& bone = poses_[index].bones[source_scale_bones_[track]];
            for (int axis = 0; axis < 3; ++axis) bone.scale.v[axis] = key.scale[axis];
            bone.hasChanged = 1;
        }
        inputs[sampled] = &poses_[index];
        weights[sampled] = samples[sample].weight;
        ++sampled;
        if (sample == 0) current_anim_ = index;
    }
    if (sampled == 0) {
        t3d_skeleton_reset(&skeleton_);
    } else {
        for (int bone = 0; bone < skeleton_.skeletonRef->boneCount; ++bone) {
            auto& result = skeleton_.bones[bone];
            AnimationPose poses[PlayerAnimation::kClipCount];
            for (int i = 0; i < sampled; ++i) {
                const auto& input = inputs[i]->bones[bone];
                for (int axis = 0; axis < 3; ++axis) {
                    poses[i].scale[axis] = input.scale.v[axis];
                    poses[i].position[axis] = input.position.v[axis];
                }
                for (int axis = 0; axis < 4; ++axis) poses[i].rotation.v[axis] = input.rotation.v[axis];
            }
            const auto blended = BlendAnimationPoses(poses, weights, sampled);
            for (int axis = 0; axis < 3; ++axis) {
                result.scale.v[axis] = blended.scale[axis];
                result.position.v[axis] = blended.position[axis];
            }
            for (int axis = 0; axis < 4; ++axis) result.rotation.v[axis] = blended.rotation.v[axis];
            result.hasChanged = 1;
        }
    }
    pose_cache_.Store(samples, count);
    // Only render sampling advances buffered matrices, never simulation ticks.
    t3d_skeleton_update(&skeleton_);
}

void AnimatedModel::UpdateMatrix(const Vec3& pos, float scale, float yaw_rad) {
    UpdateMatrix(pos, Vec3{scale, scale, scale}, yaw_rad);
}

void AnimatedModel::UpdateMatrix(const Vec3& pos, const Vec3& scale, float yaw_rad) {
    if (!model_ || !matrix_fp_) return;
    const float rot[3] = {0.0f, yaw_rad, 0.0f};
    const float scl[3] = {scale.x, scale.y, scale.z};
    const float p[3] = {pos.x, pos.y, pos.z};
    t3d_mat4_from_srt_euler(&model_matrix_, scl, rot, p);
    t3d_mat4_to_fixed(matrix_fp_, &model_matrix_);
}

bool AnimatedModel::HeadPoint(const Vec3& source_local_offset, Vec3& world_point) const {
    if (!IsLoaded() || !skeleton_.bones || head_bone_index_ < 0 || !matrix_fp_) return false;
    world_point = PlayerHeadAnchor(skeleton_.bones[head_bone_index_].matrix.m,
                                   model_matrix_.m, source_local_offset);
    return std::isfinite(world_point.x) && std::isfinite(world_point.y) &&
           std::isfinite(world_point.z);
}

bool AnimatedModel::PrepareDraw() {
    draw_ready_ = IsLoaded();
    return draw_ready_;
}

void AnimatedModel::Draw() const {
    if (!IsLoaded() || !matrix_fp_ || !draw_ready_) return;
    t3d_matrix_push(matrix_fp_);
    t3d_skeleton_use(&skeleton_);
    t3d_model_draw_skinned(model_, &skeleton_);
    t3d_matrix_pop(1);
}

const char* AnimatedModel::CurrentClipName() const {
    return current_anim_ >= 0 ? manifest_.At(current_anim_).name : "";
}

}  // namespace madeline_cube
