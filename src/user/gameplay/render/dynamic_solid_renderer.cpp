#include "gameplay/render/dynamic_solid_renderer.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <rdpq.h>
#include <rdpq_sprite.h>
#include <t3d/t3dmath.h>

#include "gameplay/debug_flags.hpp"
#include "gameplay/world/dynamic_solid_geometry.hpp"
#include "gameplay/render/material_catalog.hpp"
#include "gameplay/render/open_world_renderer.hpp"
#include "gameplay/render/pass_camera_math.hpp"

namespace madeline_cube {
namespace {

constexpr float kVertexScale = 32.0f;
constexpr float kInvVertexScale = 1.0f / kVertexScale;
constexpr uint32_t kMaxFaceVertices = 68;

const rdpq_texparms_t kWrapTexparms = {
    0, 0,
    {0.0f, 0, REPEAT_INFINITE, false},
    {0.0f, 0, REPEAT_INFINITE, false},
};
const rdpq_texparms_t kClampTexparms = {
    0, 0,
    {0.0f, 0, 0, false},
    {0.0f, 0, 0, false},
};
const rdpq_texparms_t kRepeatSClampTTexparms = {
    0, 0,
    {0.0f, 0, REPEAT_INFINITE, false},
    {0.0f, 0, 0, false},
};
const rdpq_texparms_t kClampSRepeatTTexparms = {
    0, 0,
    {0.0f, 0, 0, false},
    {0.0f, 0, REPEAT_INFINITE, false},
};

const rdpq_texparms_t* TextureParms(
    const MaterialCatalog::MaterialProfile* profile) {
    if (!profile || (profile->repeat_s && profile->repeat_t)) return &kWrapTexparms;
    if (profile->repeat_s) return &kRepeatSClampTTexparms;
    if (profile->repeat_t) return &kClampSRepeatTTexparms;
    return &kClampTexparms;
}

uint32_t MaterialColor(uint16_t material_id) {
    switch (material_id) {
        case 0: return 0x968773FF;
        case 1: return 0xEBF0FAFF;
        case 2: return 0x6E6455FF;
        case 3: return 0x788291FF;
        case 4: return 0x5F5A55FF;
        default: return 0xFFFFFFFF;
    }
}

void EmitMaterialState(uint16_t material_id, const MaterialCatalog* materials,
                       RenderCounters* counters) {
    sprite_t* sprite = materials ? materials->MaterialFor(material_id) : nullptr;
    if (sprite) {
        const auto* profile = materials->ProfileFor(material_id);
        const auto flags = static_cast<T3DDrawFlags>(
            T3D_FLAG_TEXTURED | T3D_FLAG_DEPTH |
            (kEnableBackCull ? T3D_FLAG_CULL_BACK : 0));
        t3d_state_set_drawflags(flags);
        rdpq_mode_filter(profile && profile->bilinear_filter
                             ? FILTER_BILINEAR : FILTER_POINT);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        assert((reinterpret_cast<uintptr_t>(sprite->data) & 0x7) == 0);
        rdpq_sprite_upload(TILE0, sprite, TextureParms(profile));
        const int mip_count = sprite_get_lod_count(sprite);
        rdpq_mode_mipmap(mip_count > 1 ? MIPMAP_INTERPOLATE : MIPMAP_NONE,
                         mip_count > 1 ? mip_count : 0);
        t3d_state_set_drawflags(flags);
        if (counters) ++counters->texture_uploads;
        return;
    }

    const auto flags = static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_DEPTH |
        (kEnableBackCull ? T3D_FLAG_CULL_BACK : 0));
    rdpq_mode_mipmap(MIPMAP_NONE, 0);
    t3d_state_set_drawflags(flags);
    rdpq_mode_combiner(RDPQ_COMBINER1((PRIM,0,SHADE,0),(PRIM,0,SHADE,0)));
    const uint32_t color = MaterialColor(material_id);
    rdpq_set_prim_color(RGBA32(static_cast<uint8_t>(color >> 24),
                              static_cast<uint8_t>(color >> 16),
                              static_cast<uint8_t>(color >> 8),
                              static_cast<uint8_t>(color)));
}

void SetPackedVertex(T3DVertPacked& pair, bool second,
                     const DynamicSolidVertex& source) {
    int16_t* position = second ? pair.posB : pair.posA;
    position[0] = static_cast<int16_t>(source.position.x * kVertexScale);
    position[1] = static_cast<int16_t>(source.position.y * kVertexScale);
    position[2] = static_cast<int16_t>(source.position.z * kVertexScale);
    T3DVec3 normal = {{source.normal.x, source.normal.y, source.normal.z}};
    const uint16_t packed_normal = t3d_vert_pack_normal(&normal);
    int16_t* uv = second ? pair.stB : pair.stA;
    uv[0] = static_cast<int16_t>(source.u * kVertexScale);
    uv[1] = static_cast<int16_t>(source.v * kVertexScale);
    if (second) {
        pair.normB = packed_normal;
        pair.rgbaB = 0xFFFFFFFF;
    } else {
        pair.normA = packed_normal;
        pair.rgbaA = 0xFFFFFFFF;
    }
}

}  // namespace

bool DynamicSolidRenderer::Prepare(const DynamicSolidGeometry& geometry) {
    Free();
    if (geometry.ActorCount() > was_visible_.size()) return false;
    if (!geometry.IsLoaded()) return geometry.ActorCount() == 0;
    const uint32_t count = geometry.VertexCount();
    if (count == 0) return geometry.ActorCount() == 0;

    const DynamicSolidFace* faces = geometry.Faces();
    const DynamicSolidVertex* source = geometry.Vertices();
    for (uint32_t i = 0; i < geometry.FaceCount(); ++i) {
        if (faces[i].vertex_count < 3 || faces[i].vertex_count > kMaxFaceVertices)
            return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const DynamicSolidVertex& v = source[i];
        const float packed_u = v.u * kVertexScale;
        const float packed_v = v.v * kVertexScale;
        if (!std::isfinite(packed_u) || !std::isfinite(packed_v) ||
            packed_u < -32768.0f || packed_u > 32767.0f ||
            packed_v < -32768.0f || packed_v > 32767.0f ||
            std::fabs(v.position.x * kVertexScale) > 32767.0f ||
            std::fabs(v.position.y * kVertexScale) > 32767.0f ||
            std::fabs(v.position.z * kVertexScale) > 32767.0f) return false;
    }

    const uint32_t pair_count = (count + 1u) / 2u;
    vertices_ = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * pair_count));
    if (!vertices_) return false;
    assert((reinterpret_cast<uintptr_t>(vertices_) & 0xF) == 0);

    actor_matrices_ = static_cast<T3DMat4FP*>(malloc_uncached(
        sizeof(T3DMat4FP) * geometry.ActorCount()));
    if (!actor_matrices_) {
        Free();
        return false;
    }
    assert((reinterpret_cast<uintptr_t>(actor_matrices_) & 0xF) == 0);
    actor_count_ = geometry.ActorCount();

    for (uint32_t i = 0; i < count; ++i)
        SetPackedVertex(vertices_[i / 2u], (i & 1u) != 0, source[i]);
    if (count & 1u) {
        SetPackedVertex(vertices_[count / 2u], true, source[count - 1u]);
    }
    vertex_count_ = count;
    geometry_ = &geometry;
    was_visible_.fill(0);
    visibility_known_.fill(0);
    (void)kInvVertexScale;
    return true;
}

void DynamicSolidRenderer::Draw(const DynamicSolidGeometry& geometry,
                                const MaterialCatalog* materials,
                                const CameraDesc& camera,
                                RenderCounters* counters,
                                const Vec3* player_position) {
    if (!vertices_ || !actor_matrices_ || geometry_ != &geometry ||
        geometry.ActorCount() != actor_count_) return;
    const DynamicSolidSpec* actors = geometry.Actors();
    const DynamicSolidView* views = geometry.Views();
    const DynamicSolidFace* faces = geometry.Faces();
    int current_material = -1;

    for (uint16_t a = 0; a < geometry.ActorCount(); ++a) {
        const DynamicSolidView& view = views[a];
        const bool visible = DynamicSolidVisibleInCamera(
            view, camera.pos, camera.target, camera.fov_deg, camera.near,
            camera.far, player_position, was_visible_[a] != 0);
        if (!visibility_known_[a] || visible != (was_visible_[a] != 0)) {
            const char* reason = !view.visible ? "map-hidden"
                                : visible ? "visible"
                                          : "outside-camera-and-player-grace";
            debugf("[dsolid-visibility] solid=%u source=%lu visible=%u reason=%s "
                   "bounds=(%.1f,%.1f,%.1f..%.1f,%.1f,%.1f) "
                   "camera=(%.1f,%.1f,%.1f) player_valid=%u player=(%.1f,%.1f,%.1f)\n",
                   static_cast<unsigned int>(view.solid_id),
                   static_cast<unsigned long>(view.source_id),
                   visible ? 1u : 0u, reason,
                   view.bounds.min.x, view.bounds.min.y, view.bounds.min.z,
                   view.bounds.max.x, view.bounds.max.y, view.bounds.max.z,
                   camera.pos.x, camera.pos.y, camera.pos.z,
                   player_position ? 1u : 0u,
                   player_position ? player_position->x : 0.0f,
                   player_position ? player_position->y : 0.0f,
                   player_position ? player_position->z : 0.0f);
            visibility_known_[a] = 1;
        }
        was_visible_[a] = visible ? 1 : 0;
        if (!visible) continue;

        const Vec3 origin = DynamicSolidRenderOrigin(view, camera.pos);
        T3DMat4 model;
        const float scale[3] = {kInvVertexScale, kInvVertexScale,
                                kInvVertexScale};
        const float rotation[3] = {0.0f, view.yaw_radians, 0.0f};
        const float position[3] = {origin.x, origin.y, origin.z};
        t3d_mat4_from_srt_euler(&model, scale, rotation, position);
        T3DMat4FP& model_fp = actor_matrices_[a];
        t3d_mat4_to_fixed(&model_fp, &model);
        t3d_matrix_push(&model_fp);

        const DynamicSolidSpec& actor = actors[a];
        const uint32_t actor_face_end = actor.dsol_first_face + actor.dsol_face_count;
        for (uint32_t fi = actor.dsol_first_face; fi < actor_face_end;) {
            const DynamicSolidFaceBatch batch = BuildDynamicSolidFaceBatch(
                faces, fi, actor_face_end, T3D_VERTEX_CACHE_SIZE);
            if (!batch.valid) {
                t3d_matrix_pop(1);
                return;
            }
            const uint16_t material_id = faces[fi].material_id;
            if (material_id != current_material) {
                EmitMaterialState(material_id, materials, counters);
                current_material = material_id;
            }

            t3d_vert_load(vertices_ + batch.first_vertex / 2u, 0,
                          batch.vertex_count);
            if (counters) {
                ++counters->near_batches;
                ++counters->vert_loads;
            }
            for (uint32_t face_index = fi; face_index < batch.end_face;
                 ++face_index) {
                const DynamicSolidFace& face = faces[face_index];
                const uint32_t base = face.first_vertex - batch.first_vertex;
                for (uint32_t tri = 0; tri + 2u < face.vertex_count; ++tri)
                    t3d_tri_draw(base, base + tri + 1u, base + tri + 2u);
            }
            t3d_tri_sync();
            if (counters) ++counters->syncs;
            fi = batch.end_face;
        }
        t3d_matrix_pop(1);
    }
}

void DynamicSolidRenderer::Free() {
    if (vertices_) free_uncached(vertices_);
    if (actor_matrices_) free_uncached(actor_matrices_);
    vertices_ = nullptr;
    actor_matrices_ = nullptr;
    actor_count_ = 0;
    vertex_count_ = 0;
    geometry_ = nullptr;
    was_visible_.fill(0);
    visibility_known_.fill(0);
}

}  // namespace madeline_cube
