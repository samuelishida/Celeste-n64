#include "gameplay/render/lvl_room_renderer.hpp"

#include <cstdio>
#include <cstring>
#include <libdragon.h>
#include <rdpq.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

#include "gameplay/world/entity_ids.hpp"

namespace madeline_cube {

namespace {

struct LvlVertex { float x, y, z, u, v; };
struct LvlFace   { uint32_t vs, vc; uint16_t mid, flags; float nx, ny, nz; };

uint32_t ReadU32(FILE* f) {
    uint8_t b[4]; fread(b, 1, 4, f);
    return (uint32_t(b[0])<<24)|(uint32_t(b[1])<<16)|(uint32_t(b[2])<<8)|b[3];
}
uint16_t ReadU16(FILE* f) {
    uint8_t b[2]; fread(b, 1, 2, f);
    return uint16_t((uint16_t(b[0])<<8)|b[1]);
}

}  // namespace

bool LvlRoomRenderer::Load(const char* lvl_path, const Vec3& render_origin) {
    render_origin_ = render_origin;
    FILE* f = fopen(lvl_path, "rb");
    if (!f) { debugf("[lvlroom] open FAILED: %s\n", lvl_path); return false; }

    // Header
    char magic[4]; fread(magic, 1, 4, f);
    if (magic[0]!='L'||magic[1]!='V'||magic[2]!='L'||magic[3]!='2') {
        debugf("[lvlroom] bad magic\n"); fclose(f); return false;
    }
    uint32_t ver = ReadU32(f);
    if (ver != 2) { debugf("[lvlroom] bad version %lu\n", (unsigned long)ver); fclose(f); return false; }

    ReadU32(f);  // collider_count (unused)
    uint32_t face_count   = ReadU32(f);
    uint32_t vertex_count = ReadU32(f);
    uint32_t entity_count = ReadU32(f);
    uint32_t string_count = ReadU32(f);

    // Skip atmosphere fields (16 bytes)
    fseek(f, 16, SEEK_CUR);

    uint32_t off_strings  = ReadU32(f);
    /*off_colliders*/ ReadU32(f);
    uint32_t off_faces    = ReadU32(f);
    uint32_t off_vertices = ReadU32(f);
    /*off_entities*/ ReadU32(f);
    /*off_props*/    ReadU32(f);

    (void)entity_count;
    (void)string_count;
    (void)off_strings;

    if (face_count == 0 || vertex_count == 0) {
        debugf("[lvlroom] empty level\n"); fclose(f); return false;
    }

    // Read faces
    LvlFace* faces = static_cast<LvlFace*>(malloc(sizeof(LvlFace) * face_count));
    fseek(f, (long)off_faces, SEEK_SET);
    for (uint32_t i = 0; i < face_count; ++i) {
        LvlFace& lf = faces[i];
        lf.vs    = ReadU32(f);
        lf.vc    = ReadU32(f);
        lf.mid   = ReadU16(f);
        lf.flags = ReadU16(f);
        fread(&lf.nx, 4, 1, f);
        fread(&lf.ny, 4, 1, f);
        fread(&lf.nz, 4, 1, f);
    }

    // Read vertices
    LvlVertex* lvl_verts = static_cast<LvlVertex*>(malloc(sizeof(LvlVertex) * vertex_count));
    fseek(f, (long)off_vertices, SEEK_SET);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        LvlVertex& lv = lvl_verts[i];
        fread(&lv.x, 4, 1, f);
        fread(&lv.y, 4, 1, f);
        fread(&lv.z, 4, 1, f);
        fread(&lv.u, 4, 1, f);
        fread(&lv.v, 4, 1, f);
    }
    fclose(f);

    // Build T3DVertPacked pairs, subtracting the render origin so the full
    // map's absolute world coordinates do not overflow the int16 packing.
    pair_count_ = (vertex_count + 1) / 2;
    vert_count_ = vertex_count;
    verts_ = static_cast<T3DVertPacked*>(
        malloc_uncached(sizeof(T3DVertPacked) * pair_count_));
    if (!verts_) { free(faces); free(lvl_verts); return false; }

    for (uint32_t pi = 0; pi < pair_count_; ++pi) {
        T3DVertPacked& p = verts_[pi];
        uint32_t ia = pi * 2;
        uint32_t ib = pi * 2 + 1;
        const LvlVertex& va = lvl_verts[ia];
        const LvlVertex& vb = (ib < vertex_count) ? lvl_verts[ib] : lvl_verts[ia];

        auto toFp = [](float v) -> int16_t { return static_cast<int16_t>(v * kPosScale); };

        p.posA[0] = toFp(va.x - render_origin.x);
        p.posA[1] = toFp(va.y - render_origin.y);
        p.posA[2] = toFp(va.z - render_origin.z);
        p.posB[0] = toFp(vb.x - render_origin.x);
        p.posB[1] = toFp(vb.y - render_origin.y);
        p.posB[2] = toFp(vb.z - render_origin.z);
        p.normA = 0; p.normB = 0;
        p.rgbaA = 0xFFFFFFFF; p.rgbaB = 0xFFFFFFFF;
        p.stA[0] = static_cast<int16_t>(va.u * 1024.0f);
        p.stA[1] = static_cast<int16_t>(va.v * 1024.0f);
        p.stB[0] = static_cast<int16_t>(vb.u * 1024.0f);
        p.stB[1] = static_cast<int16_t>(vb.v * 1024.0f);
    }

    // Apply face normals
    for (uint32_t fi = 0; fi < face_count; ++fi) {
        const LvlFace& lf = faces[fi];
        T3DVec3 n = {{lf.nx, lf.ny, lf.nz}};
        uint16_t packed = t3d_vert_pack_normal(&n);
        for (uint32_t vi = lf.vs; vi < lf.vs + lf.vc && vi < vertex_count; ++vi) {
            if (vi & 1) verts_[vi / 2].normB = packed;
            else        verts_[vi / 2].normA = packed;
        }
    }

    // Build batches: one batch per face. The .lvl stores each face as an
    // independent fan of vertices (vs .. vs+vc-1). Merging consecutive
    // same-material faces and fanning across the whole range creates
    // garbage triangles between unrelated faces, so we draw each face
    // as its own fan. Material state changes are cheap enough for now.
    // Faces that exceed the batch cap are counted as discarded (must be 0
    // for a validated artifact) rather than silently truncated.
    batch_count_ = 0;
    discarded_faces_ = 0;
    for (uint32_t fi = 0; fi < face_count; ++fi) {
        const LvlFace& lf = faces[fi];
        if (lf.vc < 3) continue;
        if (batch_count_ >= kMaxBatches) {
            ++discarded_faces_;
            continue;
        }
        batches_[batch_count_++] = {lf.vs, lf.vc, lf.vc - 2, lf.mid};
    }

    free(faces);
    free(lvl_verts);

    // Model matrix: compensate for kPosScale AND translate back to world
    // space. The vertices were rebased by `render_origin` before packing, so
    // the matrix must apply scale kInvScale then translation `render_origin`
    // to map the packed int16 (already (world - origin) * kPosScale) back to
    // world coordinates. Without the translation, the chunk is drawn at
    // `world - render_origin` while the player/camera are world-space, which
    // manifests as "only a small piece" + apparent fall-through.
    matrix_fp_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    T3DMat4 m;
    const float s[3] = {kInvScale, kInvScale, kInvScale};
    const float r[3] = {0, 0, 0};
    const float p[3] = {render_origin_.x, render_origin_.y, render_origin_.z};
    t3d_mat4_from_srt_euler(&m, s, r, p);
    t3d_mat4_to_fixed(matrix_fp_, &m);

    debugf("[lvlroom] loaded: %lu verts, %d batches, %lu faces, %d discarded\n",
           (unsigned long)vertex_count, batch_count_, (unsigned long)face_count,
           discarded_faces_);
    return true;
}

void LvlRoomRenderer::Free() {
    if (verts_) { free_uncached(verts_); verts_ = nullptr; }
    if (matrix_fp_) { free_uncached(matrix_fp_); matrix_fp_ = nullptr; }
    batch_count_ = 0;
    vert_count_ = 0;
    pair_count_ = 0;
}

// Per-material vertex colors (RGBA8888, same as bake_glb.py MATERIAL_COLORS)
static uint32_t material_color(uint16_t mat_id) {
    // mat_id 0=rock_1, 1=snow_1, 2=rock_2, 3=metal_floor_1, 4=floor_dirty_concrete
    switch (mat_id) {
        case 0: return 0x968773FF;  // rock_1: brown
        case 1: return 0xEBF0FAFF;  // snow_1: white-blue
        case 2: return 0x6E6455FF;  // rock_2: dark brown
        case 3: return 0x788291FF;  // metal_floor_1: grey-blue
        case 4: return 0x5F5A55FF;  // floor_dirty_concrete: dark grey
        default: return 0xFFFFFFFF;
    }
}

void LvlRoomRenderer::Draw() const {
    if (!verts_ || !matrix_fp_) return;

    t3d_matrix_push(matrix_fp_);

    for (int b = 0; b < batch_count_; ++b) {
        const Batch& batch = batches_[b];
        if (batch.tri_count == 0) continue;

        // Set per-material primColor
        uint32_t color = material_color(batch.material_id);
        rdpq_set_prim_color(RGBA32(
            (uint8_t)(color >> 24),
            (uint8_t)(color >> 16),
            (uint8_t)(color >> 8),
            (uint8_t)(color)
        ));

        // Load vertex pairs for this batch.
        // The base_vertex accounts for odd/even alignment in the pair-packed format.
        uint32_t base_vertex = batch.first_vertex & 1u;
        uint32_t load_count = ((base_vertex + batch.vertex_count + 1u) / 2u) * 2u;
        if (load_count > 70) load_count = 70;  // RSP DMEM limit per load
        t3d_vert_load(verts_ + batch.first_vertex / 2, 0, load_count);

        // Fan triangulation
        for (uint32_t t = 0; t < batch.tri_count; ++t) {
            t3d_tri_draw(base_vertex, base_vertex + t + 1, base_vertex + t + 2);
        }
        t3d_tri_sync();
    }

    t3d_matrix_pop(1);
}

}  // namespace madeline_cube
