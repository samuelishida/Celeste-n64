#include "gameplay/world/map_runtime.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

#ifdef __mips__
#include <asset.h>
#endif

#include "gameplay/debug_flags.hpp"  // kEnableCompressedCells (memory-opt Inc 2)
#include "gameplay/world/level_loader.hpp"  // LoadLevelInto
#include "gameplay/physics/coll_mesh.hpp"
#include "gameplay/render/render_origin_math.hpp"  // ResolveCellIndex (canonical)
#include "gameplay/world/artifact_hash.hpp"

#ifdef __mips__
#include <libdragon.h>
#define MR_LOG debugf
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
    MapRuntime* staged = new (std::nothrow) MapRuntime();
    if (!staged) return false;
    const bool loaded = staged->InitInPlace(mappack_path, build_dir);
    if (!loaded || !SwapState(*staged)) {
        delete staged;
        return false;
    }
    delete staged;  // Releases previous active state after successful swap.
    return true;
}

bool MapRuntime::InitInPlace(const char* mappack_path, const char* build_dir) {
    Reset();

    if (build_dir) {
        std::strncpy(build_dir_, build_dir, sizeof(build_dir_) - 1);
        build_dir_[sizeof(build_dir_) - 1] = '\0';
    }

    if (!LoadMapPackV2(mappack_path, spec_)) {
        MR_LOG("[map_runtime] map-pack manifest load FAILED: %s\n", mappack_path);
        return false;
    }

    // Load and verify the one global CMSH before publishing staged map state.
    // Compressed DFS assets are hashed after asset_load decompression, matching
    // the raw artifact hash recorded by the bake.
    std::string gpath = LocalizePath(spec_.global_colmesh_path, build_dir);
    bool loaded = false;
#ifdef __mips__
    if (kEnableCompressedCells) {
        int sz = 0;
        void* data = asset_load(gpath.c_str(), &sz);
        if (data) {
            const bool hash_ok = sz >= 0 &&
                static_cast<uint32_t>(sz) == spec_.global_colmesh_size &&
                artifact_hash::Crc32(static_cast<const uint8_t*>(data),
                                     static_cast<size_t>(sz)) ==
                    spec_.global_colmesh_crc32;
            if (hash_ok) {
                loaded = collision_.LoadFromBuffer(
                    static_cast<const uint8_t*>(data), static_cast<size_t>(sz));
            } else {
                MR_LOG("[map_runtime] global collision hash/size mismatch: %s\n",
                       gpath.c_str());
            }
            free(data);
        } else {
            MR_LOG("[map_runtime] asset_load FAILED (falling back to fopen): %s\n",
                   gpath.c_str());
        }
    }
#endif
    if (!loaded) {
        uint32_t actual_crc = 0, actual_size = 0;
        if (!artifact_hash::Crc32File(gpath.c_str(), actual_crc, actual_size) ||
            actual_crc != spec_.global_colmesh_crc32 ||
            actual_size != spec_.global_colmesh_size) {
            MR_LOG("[map_runtime] global collision hash/size mismatch: %s\n",
                   gpath.c_str());
            return false;
        }
        loaded = collision_.Load(gpath.c_str());
    }
    if (!loaded) {
        MR_LOG("[map_runtime] global collision load FAILED: %s\n", gpath.c_str());
        return false;
    }
    const physics::CollMesh* global_mesh = collision_.Mesh();
    if (!global_mesh || !global_mesh->header ||
        global_mesh->header->vertex_count != spec_.global_vertex_count ||
        global_mesh->header->triangle_count != spec_.global_triangle_count ||
        global_mesh->header->bvh_node_count != spec_.global_bvh_node_count) {
        MR_LOG("[map_runtime] global collision count mismatch: %s\n", gpath.c_str());
        return false;
    }
    MR_LOG("[map_runtime] global collision loaded: %lu tris, %lu verts\n",
           (unsigned long)spec_.global_triangle_count,
           (unsigned long)spec_.global_vertex_count);

    if (spec_.dsol_path[0] != '\0') {
        std::string dsol_path = LocalizePath(spec_.dsol_path, build_dir);
        if (!collision_.Mesh() ||
            !dynamic_geometry_.Load(dsol_path.c_str(), spec_, *collision_.Mesh())) {
            MR_LOG("[map_runtime] DSOL load/validation FAILED: %s\n", dsol_path.c_str());
            return false;
        }
        MR_LOG("[map_runtime] DSOL loaded: %u actors, %lu faces, %lu vertices\n",
               dynamic_geometry_.ActorCount(),
               (unsigned long)dynamic_geometry_.FaceCount(),
               (unsigned long)dynamic_geometry_.VertexCount());
    } else if (spec_.dynamic_solid_count != 0) {
        MR_LOG("[map_runtime] dynamic-solid records lack DSOL path\n");
        return false;
    }

    // OG SpikeBlock.Added flips its axis-selected direction when a static
    // SolidRayCast hits within 10 game units after map scaling. Hazard brushes are excluded
    // from collision, so this ray sees only actual Solid geometry.
    Room hazard_ray_room{};
    hazard_ray_room.coll_mesh = collision_.Mesh();
    int spike_count = 0;
    int spike_direction_flips = 0;
    for (int i = 0; i < spec_.hazard_count; ++i) {
        HazardVolumeSpec& hazard = spec_.hazards[i];
        if (hazard.kind != kHazardSpikeBlock) continue;
        ++spike_count;
        const Vec3 center = {
            (hazard.bounds.min.x + hazard.bounds.max.x) * 0.5f,
            (hazard.bounds.min.y + hazard.bounds.max.y) * 0.5f,
            (hazard.bounds.min.z + hazard.bounds.max.z) * 0.5f,
        };
        const GroundHit hit = RaycastRoomSource(
            hazard_ray_room, center, hazard.spike_direction,
            10.0f, BackfacePolicy::Ignore);
        if (hit.hit) {
            hazard.spike_direction.x = -hazard.spike_direction.x;
            hazard.spike_direction.y = -hazard.spike_direction.y;
            hazard.spike_direction.z = -hazard.spike_direction.z;
            ++spike_direction_flips;
        }
    }
    spec_.spike_direction_flip_count = spike_direction_flips;
    MR_LOG("[hazard] volumes=%d spikes=%d ray_flips=%d\n",
           spec_.hazard_count, spike_count, spike_direction_flips);

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
    dynamic_geometry_.Reset();
    active_ = ActiveRoomView{};
    active_committed_ = false;
    spec_ = MapSpecV2{};
}

bool MapRuntime::SwapState(MapRuntime& other) {
    static_assert(std::is_trivially_copyable<MapSpecV2>::value,
                  "MapSpecV2 swap relies on POD storage");
    void* temporary = malloc(sizeof(MapSpecV2));
    if (!temporary) return false;
    std::memcpy(temporary, &spec_, sizeof(MapSpecV2));
    std::memcpy(&spec_, &other.spec_, sizeof(MapSpecV2));
    std::memcpy(&other.spec_, temporary, sizeof(MapSpecV2));
    free(temporary);

    collision_.Swap(other.collision_);
    dynamic_geometry_.Swap(other.dynamic_geometry_);
    std::swap(active_, other.active_);
    std::swap(active_committed_, other.active_committed_);
    if (active_committed_)
        active_.room.dynamic_solids = dynamic_geometry_.IsLoaded()
            ? &dynamic_geometry_ : nullptr;
    if (other.active_committed_)
        other.active_.room.dynamic_solids = other.dynamic_geometry_.IsLoaded()
            ? &other.dynamic_geometry_ : nullptr;
    char path[sizeof(build_dir_)] = {};
    std::memcpy(path, build_dir_, sizeof(build_dir_));
    std::memcpy(build_dir_, other.build_dir_, sizeof(build_dir_));
    std::memcpy(other.build_dir_, path, sizeof(build_dir_));
    return true;
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

    // Swap in staged room data. TileStreamer owns visual cell renderers.
    active_ = staged;
    active_committed_ = true;
    // The active room exposes a COMPATIBILITY pointer to the global mesh so
    // the existing motor/camera/respawn queries (which read room.coll_mesh)
    // resolve static geometry through the one global mesh. Room cleanup never
    // frees or replaces this pointer — WorldCollision owns it for the map
    // lifetime.
    active_.room.coll_mesh = collision_.Mesh();
    active_.room.dynamic_solids = dynamic_geometry_.IsLoaded()
        ? &dynamic_geometry_ : nullptr;
    MR_LOG("[map_runtime] active room -> %s (origin %.1f,%.1f,%.1f)\n",
           active_.id, active_.render_origin.x, active_.render_origin.y,
           active_.render_origin.z);
    return true;
}

bool MapRuntime::LoadRoomIntoActive(const V2RoomSpec& room_spec, ActiveRoomView& view) {
    // Load the .lvl into the room (entities, atmosphere, dynamic colliders).
    LevelGeometry geometry;  // scratch; discarded after room metadata load
    std::string lvl_path = LocalizePath(room_spec.lvl_path, build_dir_);
    if (!LoadLevelInto(lvl_path.c_str(), view.room, geometry)) {
        MR_LOG("[map_runtime] LoadLevelInto FAILED for %s (%s)\n",
               room_spec.id, lvl_path.c_str());
        return false;
    }

    view.loaded = true;
    view.room.coll_mesh = collision_.Mesh();
    view.room.dynamic_solids = dynamic_geometry_.IsLoaded()
        ? &dynamic_geometry_ : nullptr;
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
    if (!active_committed_) return 0;
    if (!out || max_out < 0) return -1;
    const V2RoomSpec* rs = spec_.FindRoom(active_.id);
    if (!rs) return 0;
    int count = 0;
    for (int s = 0; s < rs->spawn_count; ++s) {
        const V2SpawnSpec& sp = rs->spawns[s];
        // Only actor spawns (not Start/anchor PlayerSpawns) become actors.
        if (sp.kind != kSpawnActor) continue;
        // Map classname -> placeholder id (same as entity_dispatch).
        uint16_t placeholder_id = 0;
        if (std::strncmp(sp.classname, "Strawberry", V2SpawnSpec::kClassLen) == 0) {
            placeholder_id = 2;
        } else if (std::strncmp(sp.classname, "Refill", V2SpawnSpec::kClassLen) == 0) {
            placeholder_id = 3;
        } else if (std::strncmp(sp.classname, "Spring", V2SpawnSpec::kClassLen) == 0) {
            placeholder_id = 7;
        } else {
            continue;  // unknown actor class
        }
        if (count >= max_out) return -1;
        out[count].position = sp.position;
        out[count].source_id = sp.source_id;
        out[count].actor_flags = sp.actor_flags;
        out[count].placeholder_id = placeholder_id;
        ++count;
    }
    return count;
}

int MapRuntime::AllActorSpawns(ActorSpawn* out, int max_out) const {
    return spec_.AllActorSpawns(out, max_out);
}

}  // namespace madeline_cube
