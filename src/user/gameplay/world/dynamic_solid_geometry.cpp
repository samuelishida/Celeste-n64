#include "gameplay/world/dynamic_solid_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gameplay/world/artifact_hash.hpp"
#include "gameplay/render/lod_math.hpp"

namespace madeline_cube {
namespace {

bool ReadBytes(FILE* file, void* out, size_t count) {
    return fread(out, 1, count, file) == count;
}

uint16_t ReadU16(FILE* file, bool& ok) {
    uint8_t b[2] = {};
    if (!ReadBytes(file, b, sizeof(b))) { ok = false; return 0; }
    return static_cast<uint16_t>((static_cast<uint16_t>(b[0]) << 8) | b[1]);
}

uint32_t ReadU32(FILE* file, bool& ok) {
    uint8_t b[4] = {};
    if (!ReadBytes(file, b, sizeof(b))) { ok = false; return 0; }
    return (static_cast<uint32_t>(b[0]) << 24) |
           (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) | b[3];
}

float ReadF32(FILE* file, bool& ok) {
    const uint32_t bits = ReadU32(file, ok);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value)) ok = false;
    return value;
}

Vec3 ReadVec3(FILE* file, bool& ok) {
    return {ReadF32(file, ok), ReadF32(file, ok), ReadF32(file, ok)};
}

bool SameFloat(float a, float b) {
    return a == b;
}

bool SameVec3(const Vec3& a, const Vec3& b) {
    return SameFloat(a.x, b.x) && SameFloat(a.y, b.y) && SameFloat(a.z, b.z);
}

Vec3 TransformPoint(const Vec3& local, const Vec3& position, float cosine,
                    float sine) {
    return {position.x + cosine * local.x + sine * local.z,
            position.y + local.y,
            position.z - sine * local.x + cosine * local.z};
}

float DistanceSquaredToAabb(const Vec3& point, const AABB& bounds) {
    const float dx = point.x < bounds.min.x ? bounds.min.x - point.x
                   : point.x > bounds.max.x ? point.x - bounds.max.x : 0.0f;
    const float dy = point.y < bounds.min.y ? bounds.min.y - point.y
                   : point.y > bounds.max.y ? point.y - bounds.max.y : 0.0f;
    const float dz = point.z < bounds.min.z ? bounds.min.z - point.z
                   : point.z > bounds.max.z ? point.z - bounds.max.z : 0.0f;
    return dx * dx + dy * dy + dz * dz;
}

void RebuildBounds(DynamicSolidView& view, const DynamicSolidSpec& actor,
                   const DynamicSolidFace* faces,
                   const DynamicSolidVertex* vertices) {
    const float cosine = std::cos(view.yaw_radians);
    const float sine = std::sin(view.yaw_radians);
    bool first = true;
    for (uint32_t f = actor.dsol_first_face;
         f < actor.dsol_first_face + actor.dsol_face_count; ++f) {
        const DynamicSolidFace& face = faces[f];
        for (uint32_t v = face.first_vertex;
             v < face.first_vertex + face.vertex_count; ++v) {
            const Vec3 p = TransformPoint(vertices[v].position, view.position,
                                          cosine, sine);
            if (first) {
                view.bounds = {p, p};
                first = false;
            } else {
                view.bounds.min.x = std::fmin(view.bounds.min.x, p.x);
                view.bounds.min.y = std::fmin(view.bounds.min.y, p.y);
                view.bounds.min.z = std::fmin(view.bounds.min.z, p.z);
                view.bounds.max.x = std::fmax(view.bounds.max.x, p.x);
                view.bounds.max.y = std::fmax(view.bounds.max.y, p.y);
                view.bounds.max.z = std::fmax(view.bounds.max.z, p.z);
            }
        }
    }
}

}  // namespace

Vec3 DynamicSolidRenderOrigin(const DynamicSolidView& view,
                              const Vec3& camera_position) {
    return {view.position.x + view.visual_offset.x - camera_position.x,
            view.position.y + view.visual_offset.y - camera_position.y,
            view.position.z + view.visual_offset.z - camera_position.z};
}

Vec3 TransformDynamicSolidRenderPoint(const DynamicSolidView& view,
                                      const Vec3& local_point,
                                      const Vec3& camera_position) {
    const float cosine = std::cos(view.yaw_radians);
    const float sine = std::sin(view.yaw_radians);
    const Vec3 origin = DynamicSolidRenderOrigin(view, camera_position);
    return {origin.x + cosine * local_point.x + sine * local_point.z,
            origin.y + local_point.y,
            origin.z - sine * local_point.x + cosine * local_point.z};
}

bool DynamicSolidVisibleInCamera(const DynamicSolidView& view,
                                 const Vec3& camera_position,
                                 const Vec3& camera_target, float fov_degrees,
                                 float near_distance, float far_distance,
                                 const Vec3* player_position,
                                 bool was_visible_last_frame) {
    if (!view.visible) return false;

    // Camera follow can lag behind the player during fast travel or climbing.
    // Keep geometry immediately around the player visible independently of
    // that lag so a collidable moving platform cannot vanish under Madeline.
    constexpr float kPlayerSolidGrace = 48.0f;
    if (player_position && DistanceSquaredToAabb(*player_position, view.bounds) <=
        kPlayerSolidGrace * kPlayerSolidGrace) return true;

    // Camera look target follows player center + 12u; feet sit ~11.25u lower.
    // This secondary grace covers nearby platforms during jumps/climbs even
    // when the player itself has moved away from their AABB.
    constexpr float kTargetSolidGrace = 64.0f;
    if (DistanceSquaredToAabb(camera_target, view.bounds) <=
        kTargetSolidGrace * kTargetSolidGrace) return true;

    // Test the actual pitched 3D camera frustum. The old XZ-only cone ignored
    // camera pitch and could reject a platform that was visibly on-screen
    // while climbing. Keep a wider exit frustum than enter frustum so camera
    // motion cannot toggle a small collidable platform at the screen edge.
    constexpr float kEnterMargin = kCullMargin;
    constexpr float kExitMargin = 1.35f;
    return CellAabbInFrustum3D(camera_position, camera_target,
                               Vec3{0.0f, 1.0f, 0.0f}, fov_degrees,
                               4.0f / 3.0f, near_distance, far_distance,
                               view.bounds,
                               was_visible_last_frame ? kExitMargin
                                                      : kEnterMargin);
}

DynamicSolidGeometry::~DynamicSolidGeometry() {
    Reset();
}

void DynamicSolidGeometry::Reset() {
    free(actors_);
    free(views_);
    free(faces_);
    free(vertices_);
    actors_ = nullptr;
    views_ = nullptr;
    faces_ = nullptr;
    vertices_ = nullptr;
    actor_count_ = 0;
    face_count_ = 0;
    vertex_count_ = 0;
    next_reinsert_ordinal_ = 0;
}

void DynamicSolidGeometry::Swap(DynamicSolidGeometry& other) {
    DynamicSolidSpec* actors = actors_; actors_ = other.actors_; other.actors_ = actors;
    DynamicSolidView* views = views_; views_ = other.views_; other.views_ = views;
    DynamicSolidFace* faces = faces_; faces_ = other.faces_; other.faces_ = faces;
    DynamicSolidVertex* vertices = vertices_; vertices_ = other.vertices_; other.vertices_ = vertices;
    for (uint16_t i = 0; i < kMaxActors; ++i) {
        uint16_t solid_order = solid_id_order_[i];
        solid_id_order_[i] = other.solid_id_order_[i];
        other.solid_id_order_[i] = solid_order;
    }
    uint16_t actor_count = actor_count_; actor_count_ = other.actor_count_; other.actor_count_ = actor_count;
    uint32_t face_count = face_count_; face_count_ = other.face_count_; other.face_count_ = face_count;
    uint32_t vertex_count = vertex_count_; vertex_count_ = other.vertex_count_; other.vertex_count_ = vertex_count;
    uint32_t next_reinsert = next_reinsert_ordinal_;
    next_reinsert_ordinal_ = other.next_reinsert_ordinal_;
    other.next_reinsert_ordinal_ = next_reinsert;
}

bool DynamicSolidGeometry::Load(const char* path, const MapSpecV2& spec,
                                const physics::CollMesh& collision) {
    uint32_t crc = 0, size = 0;
    if (!artifact_hash::Crc32File(path, crc, size) ||
        crc != spec.dsol_crc32 || size != spec.dsol_size || size < 16) {
        return false;
    }

    FILE* file = fopen(path, "rb");
    if (!file) return false;
    uint8_t magic[4] = {};
    bool ok = ReadBytes(file, magic, sizeof(magic)) &&
              std::memcmp(magic, "DSOL", sizeof(magic)) == 0;
    const uint16_t version = ReadU16(file, ok);
    const uint16_t actor_count = ReadU16(file, ok);
    const uint32_t face_count = ReadU32(file, ok);
    const uint32_t vertex_count = ReadU32(file, ok);
    const uint64_t expected_size = 16ull + 40ull * actor_count +
                                   20ull * face_count + 32ull * vertex_count;
    if (!ok || version != 1 || actor_count > kMaxActors ||
        actor_count != spec.dynamic_solid_count ||
        expected_size != size || face_count > kMaxFaces ||
        vertex_count > kMaxVertices || !collision.header ||
        !collision.triangles || !collision.solid_grid) {
        fclose(file);
        return false;
    }

    uint16_t max_source_face_id = 0;
    for (uint32_t i = 0; i < collision.header->solid_grid_count; ++i)
        max_source_face_id = std::max(max_source_face_id,
                                      collision.solid_grid[i].last_source_face_id);
    const size_t source_lookup_count =
        static_cast<size_t>(max_source_face_id) + 1u;
    auto* triangle_by_source_face = static_cast<uint16_t*>(
        malloc(source_lookup_count * sizeof(uint16_t)));
    if (!triangle_by_source_face) {
        fclose(file);
        return false;
    }
    std::memset(triangle_by_source_face, 0xFF,
                source_lookup_count * sizeof(uint16_t));
    for (uint32_t i = 0; i < collision.header->triangle_count; ++i) {
        const physics::CollTriangle& triangle = collision.triangles[i];
        if (triangle.source_face_id == 0 ||
            triangle.source_face_id > max_source_face_id) continue;
        uint16_t& stable_id = triangle_by_source_face[triangle.source_face_id];
        if (stable_id == 0xFFFFu || triangle.face_id < stable_id)
            stable_id = triangle.face_id;
    }

    auto* new_actors = static_cast<DynamicSolidSpec*>(
        malloc((actor_count ? actor_count : 1) * sizeof(DynamicSolidSpec)));
    auto* new_views = static_cast<DynamicSolidView*>(
        calloc((actor_count ? actor_count : 1), sizeof(DynamicSolidView)));
    auto* new_faces = static_cast<DynamicSolidFace*>(
        malloc((face_count ? face_count : 1) * sizeof(DynamicSolidFace)));
    auto* new_vertices = static_cast<DynamicSolidVertex*>(
        malloc((vertex_count ? vertex_count : 1) * sizeof(DynamicSolidVertex)));
    if (!new_actors || !new_views || !new_faces || !new_vertices) {
        free(triangle_by_source_face);
        free(new_actors); free(new_views); free(new_faces); free(new_vertices); fclose(file);
        return false;
    }
    uint32_t next_face = 0;
    uint32_t next_vertex = 0;
    uint32_t next_reinsert = 0;
    for (uint16_t i = 0; i < actor_count && ok; ++i) {
        DynamicSolidSpec disk{};
        disk.source_id = ReadU32(file, ok);
        disk.solid_id = ReadU16(file, ok);
        if (!ReadBytes(file, &disk.kind, 1) || !ReadBytes(file, &disk.flags, 1)) ok = false;
        disk.position = ReadVec3(file, ok);
        disk.yaw_radians = ReadF32(file, ok);
        disk.dsol_first_face = ReadU32(file, ok);
        disk.dsol_face_count = ReadU32(file, ok);
        disk.dsol_first_vertex = ReadU32(file, ok);
        disk.dsol_vertex_count = ReadU32(file, ok);
        const DynamicSolidSpec& manifest = spec.dynamic_solids[i];
        if (!ok || disk.source_id != manifest.source_id ||
            disk.solid_id != manifest.solid_id || disk.kind != manifest.kind ||
            disk.flags != manifest.flags || !SameVec3(disk.position, manifest.position) ||
            !SameFloat(disk.yaw_radians, manifest.yaw_radians) ||
            disk.dsol_first_face != manifest.dsol_first_face ||
            disk.dsol_face_count != manifest.dsol_face_count ||
            disk.dsol_first_vertex != manifest.dsol_first_vertex ||
            disk.dsol_vertex_count != manifest.dsol_vertex_count ||
            disk.dsol_first_face != next_face || disk.dsol_face_count == 0 ||
            disk.dsol_first_vertex != next_vertex || disk.dsol_vertex_count == 0 ||
            disk.dsol_first_face + disk.dsol_face_count > face_count ||
            disk.dsol_first_vertex + disk.dsol_vertex_count > vertex_count) {
            ok = false;
            break;
        }
        new_actors[i] = manifest;
        new_views[i].source_id = manifest.source_id;
        new_views[i].solid_id = manifest.solid_id;
        new_views[i].position = manifest.position;
        new_views[i].velocity = {};
        new_views[i].yaw_radians = manifest.yaw_radians;
        new_views[i].reinsert_ordinal = manifest.solid_id;
        new_views[i].collidable = true;
        new_views[i].visible = true;
        if (next_reinsert <= manifest.solid_id)
            next_reinsert = static_cast<uint32_t>(manifest.solid_id) + 1u;
        next_face += disk.dsol_face_count;
        next_vertex += disk.dsol_vertex_count;

        const uint32_t cms_last = static_cast<uint32_t>(manifest.cms_first_source_face_id) +
                                  manifest.cms_face_count - 1u;
        bool solid_range_found = false;
        for (uint32_t g = 0; g < collision.header->solid_grid_count; ++g) {
            const physics::CollSolidGridRecord& grid = collision.solid_grid[g];
            if (grid.solid_id == manifest.solid_id &&
                grid.first_source_face_id == manifest.cms_first_source_face_id &&
                grid.last_source_face_id == cms_last) {
                solid_range_found = true;
                break;
            }
        }
        if (!solid_range_found) { ok = false; break; }
        for (uint32_t source_face = manifest.cms_first_source_face_id;
             source_face <= cms_last; ++source_face) {
            if (source_face >= source_lookup_count ||
                triangle_by_source_face[source_face] == 0xFFFFu) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
    }

    for (uint32_t i = 0; i < face_count && ok; ++i) {
        DynamicSolidFace& face = new_faces[i];
        face.source_id = ReadU32(file, ok);
        face.brush_index = ReadU16(file, ok);
        face.face_index = ReadU16(file, ok);
        face.cms_source_face_id = ReadU16(file, ok);
        face.material_id = ReadU16(file, ok);
        face.first_vertex = ReadU32(file, ok);
        face.vertex_count = ReadU16(file, ok);
        face.flags = ReadU16(file, ok);
        // `new_faces` is malloc'd, so the member initializers never run. Give
        // non-CMS faces deterministic values (consumers read collision_material
        // before the cms_source_face_id short-circuit).
        face.collision_face_id = 0xFFFFu;
        face.collision_material = 0;
        if (!ok || face.vertex_count < 3 || face.first_vertex > vertex_count ||
            face.vertex_count > vertex_count - face.first_vertex) {
            ok = false;
            break;
        }
        const DynamicSolidSpec* owner = nullptr;
        for (uint16_t a = 0; a < actor_count; ++a) {
            const DynamicSolidSpec& actor = new_actors[a];
            if (actor.source_id == face.source_id &&
                i >= actor.dsol_first_face &&
                i < actor.dsol_first_face + actor.dsol_face_count) {
                owner = &actor;
                break;
            }
        }
        if (!owner || face.first_vertex < owner->dsol_first_vertex ||
            face.first_vertex > owner->dsol_first_vertex + owner->dsol_vertex_count ||
            face.vertex_count > owner->dsol_first_vertex + owner->dsol_vertex_count -
                                    face.first_vertex ||
            (face.cms_source_face_id != 0 &&
             (face.cms_source_face_id < owner->cms_first_source_face_id ||
              face.cms_source_face_id >= owner->cms_first_source_face_id +
                                            owner->cms_face_count))) {
            ok = false;
        }
        if (face.cms_source_face_id != 0) {
            if (face.cms_source_face_id >= source_lookup_count ||
                triangle_by_source_face[face.cms_source_face_id] == 0xFFFFu) {
                ok = false;
                break;
            }
            face.collision_face_id = triangle_by_source_face[face.cms_source_face_id];
            // The CMSH face_id is not validated by the mesh parser, so a
            // crafted pack could point it past the triangle array (OOB read).
            if (face.collision_face_id >= collision.header->triangle_count) {
                ok = false;
                break;
            }
            face.collision_material =
                collision.triangles[face.collision_face_id].material;
            if (!(face.collision_material & physics::MAT_SOLID)) ok = false;
        }
    }

    for (uint32_t i = 0; i < vertex_count && ok; ++i) {
        DynamicSolidVertex& vertex = new_vertices[i];
        vertex.position = ReadVec3(file, ok);
        vertex.normal = ReadVec3(file, ok);
        vertex.u = ReadF32(file, ok);
        vertex.v = ReadF32(file, ok);
    }
    if (fgetc(file) != EOF || ferror(file) || next_face != face_count ||
        next_vertex != vertex_count) ok = false;
    fclose(file);
    free(triangle_by_source_face);
    if (!ok) {
        free(new_actors); free(new_views); free(new_faces); free(new_vertices);
        return false;
    }

    for (uint16_t i = 0; i < actor_count; ++i)
        RebuildBounds(new_views[i], new_actors[i], new_faces, new_vertices);

    Reset();
    actors_ = new_actors;
    views_ = new_views;
    faces_ = new_faces;
    vertices_ = new_vertices;
    actor_count_ = actor_count;
    face_count_ = face_count;
    vertex_count_ = vertex_count;
    next_reinsert_ordinal_ = std::max(next_reinsert,
                                      static_cast<uint32_t>(collision.header->solid_grid_count));
    for (uint16_t i = 0; i < actor_count_; ++i) solid_id_order_[i] = i;
    std::sort(solid_id_order_, solid_id_order_ + actor_count_,
        [this](uint16_t a, uint16_t b) {
            return views_[a].solid_id < views_[b].solid_id;
        });
    return true;
}

bool DynamicSolidGeometry::SetTransform(uint32_t source_id, const Vec3& position,
                                        float yaw_radians) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z) || !std::isfinite(yaw_radians)) return false;
    for (uint16_t i = 0; i < actor_count_; ++i) {
        DynamicSolidView& view = views_[i];
        if (view.source_id != source_id) continue;
        if (SameVec3(view.position, position) &&
            SameFloat(view.yaw_radians, yaw_radians)) return true;
        view.position = position;
        view.yaw_radians = yaw_radians;
        view.reinsert_ordinal = next_reinsert_ordinal_++;
        RebuildBounds(view, actors_[i], faces_, vertices_);
        return true;
    }
    return false;
}

const DynamicSolidView* DynamicSolidGeometry::FindViewBySourceId(uint32_t source_id) const {
    for (uint16_t i = 0; i < actor_count_; ++i)
        if (views_[i].source_id == source_id) return &views_[i];
    return nullptr;
}

}  // namespace madeline_cube
