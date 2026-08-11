#include "gameplay/render/skybox.hpp"

#include <cstdio>
#include <libdragon.h>
#include <rdpq.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

namespace madeline_cube {

// A simple skybox: a large cube centered on the camera, drawn with a
// rotation-only transform (arch.md §29) so it never moves with the camera.
// The cube is 8 vertices (12 triangles), flat-colored.
void Skybox::Init(const char* sprite_path_or_null) {
    Free();
    (void)sprite_path_or_null;  // textured dome is future work

    // Build a cube of half-extent 1 (scaled up by the matrix to sit behind
    // the world). Vertices in local space; the matrix scales + rotates.
    static const float kHalf = 1.0f;
    const float c[8][3] = {
        {-kHalf, -kHalf, -kHalf}, { kHalf, -kHalf, -kHalf},
        { kHalf,  kHalf, -kHalf}, {-kHalf,  kHalf, -kHalf},
        {-kHalf, -kHalf,  kHalf}, { kHalf, -kHalf,  kHalf},
        { kHalf,  kHalf,  kHalf}, {-kHalf,  kHalf,  kHalf},
    };
    // 6 faces x 2 tris x 3 verts = 36 vertex indices.
    static const int faces[6][4] = {
        {0,1,2,3},  // -Z
        {5,4,7,6},  // +Z
        {4,0,3,7},  // -X
        {1,5,6,2},  // +X
        {3,2,6,7},  // +Y
        {4,5,1,0},  // -Y
    };

    // 36 vertices -> 18 pairs.
    pair_count_ = 18;
    verts_ = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * pair_count_));
    if (!verts_) { pair_count_ = 0; return; }

    T3DVec3 n = {{0, 0, 1}};
    uint16_t norm = t3d_vert_pack_normal(&n);
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        for (int t = 0; t < 2; ++t) {
            int idx[3];
            if (t == 0) { idx[0]=faces[f][0]; idx[1]=faces[f][1]; idx[2]=faces[f][2]; }
            else        { idx[0]=faces[f][0]; idx[1]=faces[f][2]; idx[2]=faces[f][3]; }
            for (int k = 0; k < 3; ++k) {
                int pair = vi / 2;
                bool second = (vi & 1);
                T3DVertPacked& p = verts_[pair];
                int16_t px = static_cast<int16_t>(c[idx[k]][0] * 100.0f);
                int16_t py = static_cast<int16_t>(c[idx[k]][1] * 100.0f);
                int16_t pz = static_cast<int16_t>(c[idx[k]][2] * 100.0f);
                if (!second) {
                    p.posA[0]=px; p.posA[1]=py; p.posA[2]=pz;
                    p.rgbaA = 0xFFFFFFFF; p.normA = norm;
                } else {
                    p.posB[0]=px; p.posB[1]=py; p.posB[2]=pz;
                    p.rgbaB = 0xFFFFFFFF; p.normB = norm;
                }
                ++vi;
            }
        }
    }

    matrix_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    T3DMat4 m;
    t3d_mat4_identity(&m);
    // Scale the unit cube to a large sky dome radius (e.g. 2000 units) so it
    // sits behind the distant world. Rotation-only: no translation.
    t3d_mat4_scale(&m, 2000.0f, 2000.0f, 2000.0f);
    t3d_mat4_to_fixed(matrix_fp_, &m);
    debugf("[skybox] initialized\n");
}

void Skybox::Free() {
    if (verts_) { free_uncached(verts_); verts_ = nullptr; }
    if (matrix_fp_) { free_uncached(matrix_fp_); matrix_fp_ = nullptr; }
    pair_count_ = 0;
}

void Skybox::Draw(const CameraDesc& cam) {
    if (!verts_ || !matrix_fp_) return;
    (void)cam;

    // Draw with Z off (sky sits behind everything) and a flat combiner.
    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);
    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(T3D_FLAG_SHADED));
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_set_prim_color(RGBA32(88, 163, 221, 0xFF));  // sky blue

    t3d_matrix_push(matrix_fp_);
    t3d_vert_load(verts_, 0, pair_count_ * 2);
    for (int f = 0; f < 6; ++f) {
        int base = f * 6;
        t3d_tri_draw(base, base + 1, base + 2);
        t3d_tri_draw(base, base + 2, base + 3);
    }
    t3d_tri_sync();
    t3d_matrix_pop(1);

    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);
}

}  // namespace madeline_cube
