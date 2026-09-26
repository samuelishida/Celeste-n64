// Temporary diagnostic: enumerate near-vertical perpendicular wall faces
// sharing a vertical edge in the staged Forsaken City collision mesh.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/world/mappack_loader.hpp"
#include "gameplay/world/world.hpp"

using namespace madeline_cube;
using namespace madeline_cube::physics;

namespace {

Vec3 Delta(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 CrossProduct(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

float Length(const Vec3& p) {
    return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
}

struct Face {
    Vec3 p[3];
    Vec3 normal;
    int id = -1;
};

struct Corner {
    Vec3 edge_mid;
    Vec3 normal_a;
    Vec3 normal_b;
    float edge_height = 0.0f;
    float start_distance = 0.0f;
    int face_a = -1;
    int face_b = -1;
};

bool SamePoint(const Vec3& a, const Vec3& b) {
    return Length(Delta(a, b)) <= 0.03f;
}

}  // namespace

int main(int argc, char** argv) {
    const char* pack_path = argc > 1 ? argv[1]
        : "build/bake-fc-1200/staging/forsyken-city.mappack";
    const char* mesh_path = argc > 2 ? argv[2]
        : "build/bake-fc-1200/staging/forsyken-city.colmesh";
    MapSpecV2 map{};
    if (!LoadMapPackV2(pack_path, map)) {
        std::printf("FAIL: cannot load %s\n", pack_path);
        return 1;
    }
    const V2SpawnSpec* start = map.FindStartSpawn();
    CollMesh* mesh = LoadCollMesh(mesh_path);
    if (!start || !mesh) {
        std::printf("FAIL: missing Start or collision mesh\n");
        FreeCollMesh(mesh);
        return 1;
    }

    std::vector<Face> walls;
    walls.reserve(mesh->header->triangle_count / 4);
    for (uint32_t i = 0; i < mesh->header->triangle_count; ++i) {
        const CollTriangle& tri = mesh->triangles[i];
        if ((tri.material & MAT_SOLID) == 0) continue;
        const Vec3 p[] = {
            mesh->world_verts[tri.i0],
            mesh->world_verts[tri.i1],
            mesh->world_verts[tri.i2],
        };
        Vec3 normal = CrossProduct(Delta(p[1], p[0]), Delta(p[2], p[0]));
        const float length = Length(normal);
        if (length <= 1.0e-5f) continue;
        normal = {normal.x / length, normal.y / length, normal.z / length};
        if (std::fabs(normal.y) >= 0.35f) continue;
        walls.push_back({{p[0], p[1], p[2]}, normal,
                         static_cast<int>(tri.face_id)});
    }

    std::vector<Corner> corners;
    for (size_t i = 0; i < walls.size(); ++i) {
        for (size_t j = i + 1; j < walls.size(); ++j) {
            const Face& a = walls[i];
            const Face& b = walls[j];
            const float dot = a.normal.x * b.normal.x + a.normal.z * b.normal.z;
            if (std::fabs(dot) > 0.45f) continue;

            Vec3 common[3]{};
            int common_count = 0;
            for (const Vec3& pa : a.p) {
                for (const Vec3& pb : b.p) {
                    if (!SamePoint(pa, pb)) continue;
                    bool duplicate = false;
                    for (int k = 0; k < common_count; ++k) {
                        if (SamePoint(common[k], pa)) duplicate = true;
                    }
                    if (!duplicate && common_count < 3) common[common_count++] = pa;
                }
            }
            if (common_count < 2) continue;

            const Vec3 edge = Delta(common[1], common[0]);
            const float edge_height = std::fabs(edge.y);
            const float horizontal_edge = std::sqrt(edge.x * edge.x + edge.z * edge.z);
            if (edge_height < 5.0f || horizontal_edge > 0.05f) continue;

            const Vec3 midpoint = {
                (common[0].x + common[1].x) * 0.5f,
                (common[0].y + common[1].y) * 0.5f,
                (common[0].z + common[1].z) * 0.5f,
            };
            const float dx = midpoint.x - start->position.x;
            const float dz = midpoint.z - start->position.z;
            corners.push_back({midpoint, a.normal, b.normal, edge_height,
                               std::sqrt(dx * dx + dz * dz), a.id, b.id});
        }
    }
    std::sort(corners.begin(), corners.end(), [](const Corner& a, const Corner& b) {
        return a.start_distance < b.start_distance;
    });
    std::printf("[real-map-corners] wall_triangles=%zu shared_vertical_edges=%zu start=(%.1f,%.1f,%.1f)\n",
                walls.size(), corners.size(), start->position.x,
                start->position.y, start->position.z);
    Room room{};
    room.coll_mesh = mesh;
    for (float x : {-30.0f, -20.0f, -12.0f, -8.0f, -4.0f, 0.0f, 4.0f}) {
        const Vec3 probe = {x, start->position.y + 3.0f, 55.2f};
        const WallHit hit = QueryClimbWall(
            room, probe, 4.0f, {0.0f, 0.0f, 1.0f});
        if (hit.hit) {
            std::printf("[real-map-corner-probe] hit=1 face=%d point=(%.1f,%.1f,%.1f) "
                        "normal=(%.2f,%.2f,%.2f) pushout=%.2f probe=(%.1f,%.1f,%.1f)\n",
                        hit.face_id, hit.point.x, hit.point.y, hit.point.z,
                        hit.plane_normal.x, hit.plane_normal.y,
                        hit.plane_normal.z, hit.pushout,
                        probe.x, probe.y, probe.z);
        }
    }
    const size_t count = std::min<size_t>(corners.size(), 30);
    for (size_t i = 0; i < count; ++i) {
        const Corner& c = corners[i];
        std::printf("[real-map-corner] rank=%zu dist=%.1f edge=(%.1f,%.1f,%.1f) h=%.1f "
                    "faces=%d,%d n=(%.2f,%.2f,%.2f)/(%.2f,%.2f,%.2f)\n",
                    i, c.start_distance, c.edge_mid.x, c.edge_mid.y,
                    c.edge_mid.z, c.edge_height, c.face_a, c.face_b,
                    c.normal_a.x, c.normal_a.y, c.normal_a.z,
                    c.normal_b.x, c.normal_b.y, c.normal_b.z);
    }
    FreeCollMesh(mesh);
    return corners.empty() ? 1 : 0;
}
