#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/physics/geom.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <dirent.h>
#include <cstring>
#include <cmath>

using namespace madeline_cube;
using namespace madeline_cube::physics;

struct AuditResult {
    int inverted = 0;
    int borderline = 0;
    int miss = 0;
};

bool audit_chunk(const char* path, AuditResult& res) {
    CollMesh* mesh = LoadCollMesh(path);
    if (!mesh) {
        std::cerr << "chunk=" << path << " load_failed" << std::endl;
        return false;
    }

    bool chunk_ok = true;
    const uint32_t tri_count = mesh->header->triangle_count;
    
    for (uint32_t i = 0; i < tri_count; ++i) {
        const CollTriangle& t = mesh->triangles[i];
        if (!(t.material & MAT_SOLID)) continue;

        Vec3 a = mesh->world_verts[t.i0];
        Vec3 b = mesh->world_verts[t.i1];
        Vec3 c = mesh->world_verts[t.i2];

        // 1. Compute geometric normal (unnormalized)
        Vec3 geo_n = Cross(Sub(b, a), Sub(c, a));
        Vec3 norm_n = Norm(geo_n);

        // Only audit floor-upward triangles (n.y > 0.5)
        if (norm_n.y <= 0.5f) continue;

        // 2. Raycast from above centroid
        Vec3 centroid = Scale(Add(Add(a, b), c), 1.0f / 3.0f);
        Vec3 origin = Add(centroid, {0.0f, 1.0f, 0.0f});
        Vec3 dir = {0.0f, -1.0f, 0.0f};

        // We use RaycastMesh (which is what RaycastRoomMesh calls)
        // Note: RaycastMesh is declared in coll_mesh.hpp
        RayHit hit = RaycastMesh(*mesh, origin, dir, 2.0f, BackfaceCull::Ignore);

        if (!hit.hit) {
            res.miss++;
            std::cout << "chunk=" << path << " face=" << i << " material=" << t.material 
                      << " normal=(" << norm_n.x << "," << norm_n.y << "," << norm_n.z 
                      << ") dot=nan reason=miss" << std::endl;
            chunk_ok = false;
        } else {
            float dot = Dot(hit.normal, dir); // ray is {0,-1,0}, so dot is -hit.normal.y
            
            // Runtime logic: if (dot >= 0.0f) return GroundHit{};
            // For a real floor (normal ~{0,1,0}), dot = {0,1,0} . {0,-1,0} = -1.0
            // So we WANT dot < 0.0f.
            if (dot >= 0.0f) {
                if (hit.normal.y < -0.5f) {
                    res.inverted++;
                    std::cout << "chunk=" << path << " face=" << i << " material=" << t.material 
                              << " normal=(" << hit.normal.x << "," << hit.normal.y << "," << hit.normal.z 
                              << ") dot=" << dot << " reason=inverted" << std::endl;
                } else {
                    res.borderline++;
                    std::cout << "chunk=" << path << " face=" << i << " material=" << t.material 
                              << " normal=(" << hit.normal.x << "," << hit.normal.y << "," << hit.normal.z 
                              << ") dot=" << dot << " reason=borderline" << std::endl;
                }
                chunk_ok = false;
            }
        }
    }

    FreeCollMesh(mesh);
    return chunk_ok;
}

int main(int argc, char** argv) {
    const char* search_dir = "filesystem/lvl/forsyken-city";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dir=") == 0 && i + 1 < argc) {
            search_dir = argv[++i];
        } else if (std::strncmp(argv[i], "--dir=", 6) == 0) {
            search_dir = argv[i] + 6;
        }
    }

    DIR* dir = opendir(search_dir);
    if (!dir) {
        std::cerr << "failed to open directory: " << search_dir << std::endl;
        return 1;
    }

    struct dirent* entry;
    int chunks_audited = 0;
    int chunks_failed = 0;

    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        if (std::strcmp(entry->d_name, "index.manifest") == 0) continue;
        
        std::string filename = entry->d_name;
        if (filename.find(".colmesh") == std::string::npos) continue;

        std::string full_path = std::string(search_dir) + "/" + filename;
        AuditResult res;
        chunks_audited++;
        if (!audit_chunk(full_path.c_str(), res)) {
            chunks_failed++;
        }
    }
    closedir(dir);

    std::cout << "\nAudit Summary:\n";
    std::cout << "Chunks audited: " << chunks_audited << "\n";
    std::cout << "Chunks with failures: " << chunks_failed << "\n";

    return (chunks_failed > 0) ? 1 : 0;
}
