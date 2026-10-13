#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"
#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/world/mappack_loader.hpp"

namespace madeline_cube {

struct DynamicSolidFace {
    uint32_t source_id = 0;
    uint16_t brush_index = 0;
    uint16_t face_index = 0;
    uint16_t cms_source_face_id = 0;
    uint16_t collision_face_id = 0xFFFFu;
    uint16_t material_id = 0;
    uint32_t first_vertex = 0;
    uint16_t vertex_count = 0;
    uint16_t flags = 0;
    uint16_t collision_material = 0;
};

struct DynamicSolidFaceBatch {
    uint32_t end_face = 0;
    uint32_t first_vertex = 0;
    uint16_t vertex_count = 0;
    bool valid = false;
};

// Group adjacent same-material faces only when one aligned Tiny3D vertex load
// can cover them. Faces retain order; caller supplies cache capacity.
inline DynamicSolidFaceBatch BuildDynamicSolidFaceBatch(
    const DynamicSolidFace* faces, uint32_t first_face, uint32_t end_face,
    uint16_t max_vertices) {
    if (!faces || first_face >= end_face || max_vertices == 0) return {};
    const DynamicSolidFace& first = faces[first_face];
    if (first.vertex_count < 3) return {};
    const uint32_t load_first = first.first_vertex & ~1u;
    uint32_t vertex_end = first.first_vertex + first.vertex_count;
    uint32_t load_count = (vertex_end - load_first + 1u) & ~1u;
    if (load_count > max_vertices) return {};

    uint32_t next_face = first_face + 1u;
    while (next_face < end_face) {
        const DynamicSolidFace& next = faces[next_face];
        if (next.material_id != first.material_id ||
            next.first_vertex != vertex_end || next.vertex_count < 3) break;
        const uint32_t next_end = next.first_vertex + next.vertex_count;
        const uint32_t next_load_count = (next_end - load_first + 1u) & ~1u;
        if (next_load_count > max_vertices) break;
        vertex_end = next_end;
        load_count = next_load_count;
        ++next_face;
    }
    return {next_face, load_first, static_cast<uint16_t>(load_count), true};
}

// Live collision state for one map-wide actor. Bounds use world XYZ; OG's
// GridPartition plane is X/-Z and is derived by query ordering code.
struct DynamicSolidView {
    uint32_t source_id = 0;
    uint16_t solid_id = 0;
    Vec3 position;
    Vec3 velocity;
    Vec3 visual_offset;  // model-only shake; collision remains at position
    float yaw_radians = 0.0f;
    AABB bounds;
    uint32_t reinsert_ordinal = 0;
    bool collidable = true;
    bool visible = true;
    bool update_off_screen = false;
};

// Host-safe render-space transform oracle. Actor-local geometry uses the same
// yaw/position transform as collision, with visual_offset applied only here.
Vec3 DynamicSolidRenderOrigin(const DynamicSolidView& view,
                              const Vec3& camera_position);
Vec3 TransformDynamicSolidRenderPoint(const DynamicSolidView& view,
                                      const Vec3& local_point,
                                      const Vec3& camera_position);
bool DynamicSolidVisibleInCamera(const DynamicSolidView& view,
                                const Vec3& camera_position,
                                const Vec3& camera_target, float fov_degrees,
                                float near_distance, float far_distance,
                                const Vec3* player_position = nullptr,
                                bool was_visible_last_frame = false);

struct DynamicSolidVertex {
    Vec3 position;
    Vec3 normal;
    float u = 0.0f;
    float v = 0.0f;
};

// Owns the DSOL sidecar geometry. Load validates whole-file CRC/size and
// cross-checks actor identity/ranges against MPP5 before replacing old data.
class DynamicSolidGeometry {
public:
    static constexpr uint16_t kMaxActors = MapSpecV2::kMaxDynamicSolids;
    static constexpr uint32_t kMaxFaces = 8192;
    static constexpr uint32_t kMaxVertices = 32768;

    DynamicSolidGeometry() = default;
    ~DynamicSolidGeometry();
    DynamicSolidGeometry(const DynamicSolidGeometry&) = delete;
    DynamicSolidGeometry& operator=(const DynamicSolidGeometry&) = delete;

    bool Load(const char* path, const MapSpecV2& spec,
              const physics::CollMesh& collision);
    void Reset();
    void Swap(DynamicSolidGeometry& other);

    bool IsLoaded() const { return actors_ != nullptr; }
    const DynamicSolidSpec* Actors() const { return actors_; }
    const DynamicSolidFace* Faces() const { return faces_; }
    const DynamicSolidVertex* Vertices() const { return vertices_; }
    const DynamicSolidView* Views() const { return views_; }
    DynamicSolidView* MutableViews() { return views_; }
    uint16_t ActorCount() const { return actor_count_; }
    uint32_t FaceCount() const { return face_count_; }
    uint32_t VertexCount() const { return vertex_count_; }
    bool SetTransform(uint32_t source_id, const Vec3& position, float yaw_radians);
    int ActorIndexBySolidId(uint16_t solid_id) const {
        uint16_t first = 0;
        uint16_t last = actor_count_;
        while (first < last) {
            const uint16_t middle = static_cast<uint16_t>(first + (last - first) / 2);
            const uint16_t actor_index = solid_id_order_[middle];
            if (views_[actor_index].solid_id < solid_id)
                first = static_cast<uint16_t>(middle + 1);
            else
                last = middle;
        }
        if (first >= actor_count_) return -1;
        const uint16_t actor_index = solid_id_order_[first];
        return views_[actor_index].solid_id == solid_id ? actor_index : -1;
    }
    const DynamicSolidView* FindViewBySolidId(uint16_t solid_id) const {
        const int actor_index = ActorIndexBySolidId(solid_id);
        return actor_index >= 0 ? &views_[actor_index] : nullptr;
    }
    const DynamicSolidView* FindViewBySourceId(uint32_t source_id) const;
    bool OwnsSolidId(uint16_t solid_id) const { return ActorIndexBySolidId(solid_id) >= 0; }

private:
    DynamicSolidSpec* actors_ = nullptr;
    DynamicSolidView* views_ = nullptr;
    DynamicSolidFace* faces_ = nullptr;
    DynamicSolidVertex* vertices_ = nullptr;
    uint16_t actor_count_ = 0;
    uint16_t solid_id_order_[kMaxActors] = {};
    uint32_t face_count_ = 0;
    uint32_t vertex_count_ = 0;
    uint32_t next_reinsert_ordinal_ = 0;
};

}  // namespace madeline_cube
