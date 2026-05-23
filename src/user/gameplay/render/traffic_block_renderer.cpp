#include "gameplay/render/traffic_block_renderer.hpp"

#include <cmath>
#include <cstring>

#include <libdragon.h>
#include <rdpq_sprite.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

namespace madeline_cube {

bool TrafficBlockRenderer::Init() {
    verts_ = static_cast<PackedVertex*>(malloc_uncached(sizeof(PackedVertex) * 24));
    if (!verts_) return false;

    uint32_t color = 0xFFFFFFFF;

    const float fx[4] = {1.0f};
    (void)fx;

    auto pack_normal = [](float x, float y, float z) -> uint16_t {
        T3DVec3 n = {{x, y, z}};
        t3d_vec3_norm(&n);
        return t3d_vert_pack_normal(&n);
    };

    const uint16_t front  = pack_normal(0.0f, 0.0f, 1.0f);
    const uint16_t back   = pack_normal(0.0f, 0.0f, -1.0f);
    const uint16_t left   = pack_normal(-1.0f, 0.0f, 0.0f);
    const uint16_t right  = pack_normal(1.0f, 0.0f, 0.0f);
    const uint16_t top    = pack_normal(0.0f, 1.0f, 0.0f);
    const uint16_t bottom = pack_normal(0.0f, -1.0f, 0.0f);

    auto fill = [&](int idx, int16_t ax, int16_t ay, int16_t az,
                    int16_t bx, int16_t by, int16_t bz, uint16_t n) {
        verts_[idx] = {.posA={ax, ay, az}, .normA=n,
                       .posB={bx, by, bz}, .normB=n,
                       .rgbaA=color, .rgbaB=color};
    };

    fill(0,  -1, -1,  1,  1, -1,  1, front);
    fill(1,   1,  1,  1, -1,  1,  1, front);
    fill(2,   1, -1, -1, -1, -1, -1, back);
    fill(3,  -1,  1, -1,  1,  1, -1, back);
    fill(4,  -1, -1, -1, -1, -1,  1, left);
    fill(5,  -1,  1,  1, -1,  1, -1, left);
    fill(6,   1, -1,  1,  1, -1, -1, right);
    fill(7,   1,  1, -1,  1,  1,  1, right);
    fill(8,  -1,  1,  1,  1,  1,  1, top);
    fill(9,   1,  1, -1, -1,  1, -1, top);
    fill(10, -1, -1, -1,  1, -1, -1, bottom);
    fill(11,  1, -1,  1, -1, -1,  1, bottom);

    for (int i = 0; i < 6; ++i) {
        int base = i * 4;
        indices_[i*6 + 0] = base + 0; indices_[i*6 + 1] = base + 1;
        indices_[i*6 + 2] = base + 2;
        indices_[i*6 + 3] = base + 2; indices_[i*6 + 4] = base + 3;
        indices_[i*6 + 5] = base + 0;
    }

    material_sprite_ = sprite_load("rom:/tex/metal_floor_1.sprite");
    if (!material_sprite_) {
        debugf("[traffic] failed to load metal_floor_1.sprite\n");
        return false;
    }

    instance_count_ = 0;
    return true;
}

void TrafficBlockRenderer::UpdateInstance(int idx, const Vec3& center, const Vec3& he) {
    if (idx < 0 || idx >= kMaxInstances) return;
    if (idx >= instance_count_) instance_count_ = idx + 1;
    if (!matrices_[idx]) {
        matrices_[idx] = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    }
    T3DMat4 model;
    float scale[3] = {he.x, he.y, he.z};
    float rot[3] = {0.0f, 0.0f, 0.0f};
    float pos[3] = {center.x, center.y, center.z};
    t3d_mat4_from_srt_euler(&model, scale, rot, pos);
    t3d_mat4_to_fixed(matrices_[idx], &model);
}

void TrafficBlockRenderer::Draw() const {
    if (!material_sprite_) return;

    rdpq_texparms_t parms = {};
    parms.s.repeats = REPEAT_INFINITE;
    parms.t.repeats = REPEAT_INFINITE;
    rdpq_sprite_upload(TILE0, material_sprite_, &parms);

    for (int i = 0; i < instance_count_; ++i) {
        if (!matrices_[i]) continue;
        t3d_matrix_push(matrices_[i]);
        t3d_vert_load(reinterpret_cast<const T3DVertPacked*>(verts_), 0, 24);
        for (int f = 0; f < 6; ++f) {
            int base = f * 4;
            t3d_tri_draw(base + 0, base + 1, base + 2);
            t3d_tri_draw(base + 2, base + 3, base + 0);
        }
        t3d_tri_sync();
        t3d_matrix_pop(1);
    }
}

void TrafficBlockRenderer::Free() {
    if (verts_) {
        free_uncached(verts_);
        verts_ = nullptr;
    }
    for (int i = 0; i < kMaxInstances; ++i) {
        if (matrices_[i]) {
            free_uncached(matrices_[i]);
            matrices_[i] = nullptr;
        }
    }
    instance_count_ = 0;
    if (material_sprite_) {
        sprite_free(material_sprite_);
        material_sprite_ = nullptr;
    }
}

}  // namespace madeline_cube
