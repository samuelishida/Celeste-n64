#include "gameplay/world/map_runtime.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

#include "gameplay/world/level_loader.hpp"  // LoadLevelInto
#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/render/render_origin_math.hpp"  // ResolveCellIndex (canonical)

#ifdef __mips__
#include <libdragon.h>
#define MR_LOG debugf
#include "gameplay/render/lvl_room_renderer.hpp"
#else
#define MR_LOG printf
#endif

namespace madeline_cube {

namespace {

// Rewrite a "rom:/lvl/<pack>/<chunk>.lvl" path to a local filesystem path
// "<build_dir>/<chunk>.lvl" for host-side loading. On device the path is used
// as-is.
std::string LocalizePath(const char* rom_path, const char* build_dir) {
    if (!build_dir || build_dir[0] == '\0') return rom_path;
    const char* slash = std::strrchr(rom_path, '/');
    std::string fname = slash ? slash + 1 : rom_path;
    return std::string(build_dir) + "/" + fname;
}

}  // namespace

MapRuntime::~MapRuntime() {
    Reset();
}

bool MapRuntime::Init(const char* mappack_path, const char* build_dir) {
    Reset();

    if (build_dir) {
        std::strncpy(build_dir_, build_dir, sizeof(build_dir_) - 1);
    }

    if (!LoadMapPackV2(mappack_path, spec_)) {
        MR_LOG("[map_runtime] v2 manifest load FAILED: %s\n", mappack_path);
        return false;
    }

    // Load the one global CMSH from the manifest path.
    std::string gpath = LocalizePath(spec_.global_colmesh_path, build_dir);
    if (!collision_.Load(gpath.c_str())) {
        MR_LOG("[map_runtime] global collision load FAILED: %s\n", gpath.c_str());
        return false;
    }
    MR_LOG("[map_runtime] global collision loaded: %lu tris, %lu verts\n",
           (unsigned long)spec_.global_triangle_count,
           (unsigned long)spec_.global_vertex_count);

    // Validate the Start spawn exists.
    if (!spec_.FindStartSpawn()) {
        MR_LOG("[map_runtime] no Start spawn in manifest\n");
        return false;
    }

    // Commit the start room as active.
    if (!CommitActive(spec_.start_room_id)) {
        MR_LOG("[map_runtime] failed to commit start room %s\n", spec_.start_room_id);
        return false;
    }

    return true;
}

void MapRuntime::Reset() {
    collision_.Reset();
    if (active_.renderer) {
#ifdef __mips__
        active_.renderer->Free();
        delete active_.renderer;
#endif
        active_.renderer = nullptr;
    }
    active_ = ActiveRoomView{};
    active_committed_ = false;
    spec_ = MapSpecV2{};
}

const char* MapRuntime::ResolveCellByPosition(const Vec3& pos) const {
    if (spec_.room_count == 0) return "";
    // Canonical cell-resolution formula (see `render_origin_math.hpp` and the
    // cross-language contract test tests/interconnected_seam_equivalence.py).
    // Grid is 2D in world XZ: ix = floor(world_x / cell),
    // iz = floor(world_z / cell), cell = chunk_size * scale.
    int ix = 0, iz = 0;
    if (!ResolveCellIndex(pos, spec_.chunk_size, spec_.scale, ix, iz)) return "";
    for (int i = 0; i < spec_.room_count; ++i) {
        if (spec_.rooms[i].cell_ix == ix && spec_.rooms[i].cell_iz == iz) {
            return spec_.rooms[i].id;
        }
    }
    return "";
}

bool MapRuntime::SetActiveByPosition(const Vec3& player_pos, const char** new_room_id) {
    if (!active_committed_) return false;
    const char* resolved = ResolveCellByPosition(player_pos);
    if (resolved[0] == '\0') return false;
    if (std::strncmp(resolved, active_.id, ActiveRoomView::kIdLen) == 0) {
        return false;  // same cell
    }
    if (new_room_id) *new_room_id = resolved;
    return true;
}

bool MapRuntime::CommitActive(const char* room_id) {
    if (!room_id || room_id[0] == '\0') return false;
    const V2RoomSpec* rs = spec_.FindRoom(room_id);
    if (!rs) {
        MR_LOG("[map_runtime] CommitActive: unknown room %s\n", room_id);
        return false;
    }

    // Stage into a temporary view; only commit on success so a failed load
    // leaves the old active room intact.
    ActiveRoomView staged;
    std::strncpy(staged.id, rs->id, ActiveRoomView::kIdLen - 1);
    staged.cell_ix = rs->cell_ix;
    staged.cell_iz = rs->cell_iz;
    staged.render_origin = rs->render_origin;
    staged.world_aabb = rs->world_aabb;

    if (!LoadRoomIntoActive(*rs, staged)) {
        MR_LOG("[map_runtime] CommitActive: load FAILED for %s; keeping old room\n",
               room_id);
        return false;
    }

    // Swap: free the old renderer, adopt the staged view.
    if (active_.renderer) {
#ifdef __mips__
        active_.renderer->Free();
        delete active_.renderer;
#endif
        active_.renderer = nullptr;
    }
    active_ = staged;
    active_committed_ = true;
    // The active room exposes a COMPATIBILITY pointer to the global mesh so
    // the existing motor/camera/respawn queries (which read room.coll_mesh)
    // resolve static geometry through the one global mesh. Room cleanup never
    // frees or replaces this pointer — WorldCollision owns it for the map
    // lifetime.
    active_.room.coll_mesh = collision_.Mesh();
    MR_LOG("[map_runtime] active room -> %s (origin %.1f,%.1f,%.1f)\n",
           active_.id, active_.render_origin.x, active_.render_origin.y,
           active_.render_origin.z);
    return true;
}

bool MapRuntime::LoadRoomIntoActive(const V2RoomSpec& room_spec, ActiveRoomView& view) {
    // Load the .lvl into the room (entities, atmosphere, dynamic colliders).
    LevelGeometry geometry;  // scratch; discarded after renderer build
    std::string lvl_path = LocalizePath(room_spec.lvl_path, build_dir_);
    if (!LoadLevelInto(lvl_path.c_str(), view.room, geometry)) {
        MR_LOG("[map_runtime] LoadLevelInto FAILED for %s (%s)\n",
               room_spec.id, lvl_path.c_str());
        return false;
    }

#ifdef __mips__
    // Build the renderer from the .lvl (N64-only), rebased to the room's
    // render origin so the full map's absolute coordinates do not overflow
    // the int16 fixed-point packing.
    view.renderer = new LvlRoomRenderer();
    if (!view.renderer->Load(room_spec.lvl_path, room_spec.render_origin)) {
        MR_LOG("[map_runtime] LvlRoomRenderer::Load FAILED for %s\n", room_spec.id);
        delete view.renderer;
        view.renderer = nullptr;
    }
#endif

    view.loaded = true;
    return true;
}

const V2SpawnSpec* MapRuntime::FindSpawnByName(const char* name) const {
    if (!name || name[0] == '\0') return nullptr;
    for (int i = 0; i < spec_.room_count; ++i) {
        for (int s = 0; s < spec_.rooms[i].spawn_count; ++s) {
            if (std::strncmp(spec_.rooms[i].spawns[s].name, name,
                             V2SpawnSpec::kNameLen) == 0) {
                return &spec_.rooms[i].spawns[s];
            }
        }
    }
    return nullptr;
}

int MapRuntime::ActiveSpawns(ActorSpawn* out, int max_out) const {
    if (!active_committed_ || !out || max_out <= 0) return 0;
    const V2RoomSpec* rs = spec_.FindRoom(active_.id);
    if (!rs) return 0;
    int count = 0;
    for (int s = 0; s < rs->spawn_count && count < max_out; ++s) {
        const V2SpawnSpec& sp = rs->spawns[s];
        // Only actor spawns (not Start/anchor PlayerSpawns) become actors.
        if (sp.kind != kSpawnActor) continue;
        out[count].position = sp.position;
        out[count].source_id = sp.source_id;
        // Map classname -> placeholder id (same as entity_dispatch).
        if (std::strncmp(sp.classname, "Strawberry", V2SpawnSpec::kClassLen) == 0) {
            out[count].placeholder_id = 2;
        } else if (std::strncmp(sp.classname, "Refill", V2SpawnSpec::kClassLen) == 0) {
            out[count].placeholder_id = 3;
        } else if (std::strncmp(sp.classname, "Spring", V2SpawnSpec::kClassLen) == 0) {
            out[count].placeholder_id = 7;
        } else {
            continue;  // unknown actor class
        }
        ++count;
    }
    return count;
}

}  // namespace madeline_cube
