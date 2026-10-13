#include "gameplay/render/player_hair_renderer.hpp"

#include <cstdlib>

#include <libdragon.h>
#include <rdpq.h>

namespace madeline_cube {

bool PlayerHairRenderer::Init() {
    if (buffer_count_ > 0) return true;
    const int buffers = static_cast<int>(display_get_num_buffers());
    if (buffers < 1 || buffers > kMaxFrameBuffers) return false;
    buffer_count_ = static_cast<uint8_t>(buffers);
    for (uint8_t index = 0; index < buffer_count_; ++index) {
        frame_buffers_[index] = static_cast<T3DVertPacked*>(
            malloc_uncached(sizeof(T3DVertPacked) * (kVerticesPerFrame / 2)));
        if (!frame_buffers_[index]) {
            Free();
            return false;
        }
    }
    identity_matrix_ = static_cast<T3DMat4FP*>(malloc_uncached(sizeof(T3DMat4FP)));
    if (!identity_matrix_) {
        Free();
        return false;
    }
    buffer_index_ = static_cast<uint8_t>(buffer_count_ - 1);
    t3d_mat4fp_identity(identity_matrix_);
    if (!sphere_.Build()) {
        Free();
        return false;
    }
    for (uint8_t buffer_index = 0; buffer_index < buffer_count_; ++buffer_index) {
        T3DVertPacked* buffer = frame_buffers_[buffer_index];
        for (int node = 0; node < PlayerHairSimulation::kMaxNodes; ++node) {
            T3DVertPacked* node_vertices = buffer + node * kSpherePairCount;
            for (int vertex = 0; vertex < kSphereVertexCount; ++vertex) {
                const Vec3& source = sphere_.positions[vertex];
                T3DVec3 normal = {{source.x, source.y, source.z}};
                *t3d_vertbuffer_get_norm(node_vertices, vertex) =
                    t3d_vert_pack_normal(&normal);
                *t3d_vertbuffer_get_color(node_vertices, vertex) = 0xFFFFFFFFu;
                int16_t* uv = t3d_vertbuffer_get_uv(node_vertices, vertex);
                uv[0] = 0;
                uv[1] = 0;
            }
        }
    }
    return true;
}

void PlayerHairRenderer::Free() {
    for (T3DVertPacked*& buffer : frame_buffers_) {
        if (buffer) free_uncached(buffer);
        buffer = nullptr;
    }
    if (identity_matrix_) {
        free_uncached(identity_matrix_);
        identity_matrix_ = nullptr;
    }
    buffer_count_ = 0;
    buffer_index_ = 0;
}

bool PlayerHairRenderer::BuildNodeVertices(T3DVertPacked* destination,
                                            const PlayerHairSimulation& hair,
                                            int node_index, const PlayerHairRotation& rotation,
                                            const Vec3& squish_og,
                                            float roundness) const {
    const Vec3 node = hair.NodeOg(node_index);
    const PlayerHairNodeTransform transform = MakePlayerHairNodeTransform(
        node, node_index, hair.NodeCount(), squish_og, roundness, rotation);

    for (int vertex = 0; vertex < kSphereVertexCount; ++vertex) {
        const Vec3& source = sphere_.positions[vertex];
        const Vec3 point_og = PlayerHairSourceVertex(source, transform);
        const Vec3 point_world = FromOgAxes(point_og);
        int16_t* packed = t3d_vertbuffer_get_pos(destination, vertex);
        if (!PackPlayerHairCoordinate(point_world.x, packed[0]) ||
            !PackPlayerHairCoordinate(point_world.y, packed[1]) ||
            !PackPlayerHairCoordinate(point_world.z, packed[2])) return false;

    }
    return true;
}

bool PlayerHairRenderer::Draw(const PlayerHairSimulation& hair,
                              const Vec3& forward_world, const Vec3& squish_og,
                              color_t color_rgba, float roundness) {
    if (!IsReady() || hair.NodeCount() < 1 || hair.NodeCount() > PlayerHairSimulation::kMaxNodes) {
        return false;
    }
    buffer_index_ = static_cast<uint8_t>((buffer_index_ + 1) % buffer_count_);
    T3DVertPacked* vertices = frame_buffers_[buffer_index_];
    const Vec3 forward_og = ToOgAxes(forward_world);
    const PlayerHairRotation rotation = MakePlayerHairRotation(forward_og);
    for (int node = 0; node < hair.NodeCount(); ++node) {
        if (!BuildNodeVertices(vertices + node * kSpherePairCount, hair, node,
                               rotation, squish_og, roundness)) {
            return false;
        }
    }

    t3d_state_set_drawflags(static_cast<T3DDrawFlags>(
        T3D_FLAG_SHADED | T3D_FLAG_DEPTH | T3D_FLAG_CULL_FRONT));
    rdpq_mode_combiner(RDPQ_COMBINER1((PRIM,0,SHADE,0),(PRIM,0,SHADE,0)));
    rdpq_set_prim_color(color_rgba);
    t3d_matrix_push_pos(1);
    t3d_matrix_set(identity_matrix_, false);

    for (int first_node = 0; first_node < hair.NodeCount(); first_node += 2) {
        if (first_node > 0) t3d_tri_sync();
        const int nodes_in_batch = hair.NodeCount() - first_node >= 2 ? 2 : 1;
        const int vertex_count = nodes_in_batch * kSphereVertexCount;
        t3d_vert_load(vertices + first_node * kSpherePairCount, 0, vertex_count);
        for (int node_offset = 0; node_offset < nodes_in_batch; ++node_offset) {
            const uint32_t base = static_cast<uint32_t>(node_offset * kSphereVertexCount);
            for (int index = 0; index < kSphereIndexCount; index += 3) {
                t3d_tri_draw(base + sphere_.indices[index],
                             base + sphere_.indices[index + 1],
                             base + sphere_.indices[index + 2]);
            }
        }
    }
    t3d_tri_sync();
    t3d_matrix_pop(1);
    return true;
}

}  // namespace madeline_cube
