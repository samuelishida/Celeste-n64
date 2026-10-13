// MPP2 compatibility, MPP3 actor flags, and fail-closed loader fixtures.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "gameplay/world/mappack_loader.hpp"
#include "gameplay/world/actor_world.hpp"

using namespace madeline_cube;

namespace {

void U8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void U16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void U32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 24));
    b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void F32(std::vector<uint8_t>& b, float f) {
    union { float f; uint32_t u; } bits = {f};
    U32(b, bits.u);
}
void Str(std::vector<uint8_t>& b, const char* value, size_t width) {
    size_t i = 0;
    while (value[i] && i < width) b.push_back(static_cast<uint8_t>(value[i++]));
    while (i++ < width) b.push_back(0);
}
void Vec(std::vector<uint8_t>& b, float x, float y, float z) {
    F32(b, x); F32(b, y); F32(b, z);
}

std::vector<uint8_t> Fixture(uint16_t version, uint8_t actor_flags,
                             const char* classname = "Refill") {
    std::vector<uint8_t> b;
    const char* magic = version == 2 ? "MPP2" :
                        (version == 3 ? "MPP3" :
                         (version == 4 ? "MPP4" : "MPP5"));
    b.insert(b.end(), magic, magic + 4);
    U16(b, version); U16(b, 2);
    Str(b, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 64);
    F32(b, 0.2f); F32(b, 1000.0f);
    Str(b, "rom:/lvl/test/test.colmesh", 64);
    U32(b, 1); U32(b, 2); U32(b, 3); U32(b, 4); U32(b, 5);
    Str(b, "cell_00_00", 16); U16(b, 1);
    Str(b, "cell_00_00", 16); Str(b, "rom:/lvl/test/cell_00_00.lvl", 64);
    Vec(b, 0, 0, 0); Vec(b, 0, 0, 0); Vec(b, 100, 100, 100);
    U32(b, 0); U32(b, 0);
    for (int i = 0; i < 4; ++i) Str(b, "", 16);
    U16(b, 2);
    U8(b, kSpawnActor); U32(b, 17); Str(b, "cell_00_00", 16);
    Vec(b, 1, 2, 3); Str(b, "refill-double", 32); Str(b, classname, 32);
    if (version >= 3) U8(b, actor_flags);
    U8(b, kSpawnActor); U32(b, 18); Str(b, "cell_00_00", 16);
    Vec(b, 4, 5, 6); Str(b, "refill-single", 32); Str(b, "Refill", 32);
    if (version >= 3) U8(b, 0);
    if (version >= 4) {
        U16(b, 2);
        U8(b, kHazardDeathBlock); U32(b, 51);
        Vec(b, 1, 2, 3); Vec(b, 4, 5, 6); Vec(b, 0, 0, 0);
        U8(b, kHazardSpikeBlock); U32(b, 52);
        Vec(b, -1, -2, -3); Vec(b, 4, 5, 6); Vec(b, 1, 0, 0);
    }
    if (version == 5) {
        Str(b, "rom:/lvl/test/test.dsol", 64);
        U32(b, 0x12345678); U32(b, 200); U16(b, 1);
        U32(b, 100); U16(b, 198); U8(b, 1); U8(b, 0);
        Vec(b, 10, 20, 30); F32(b, 0.0f);
        U16(b, 41); U16(b, 2);
        U32(b, 0); U32(b, 1); U32(b, 0); U32(b, 3);
        U32(b, 101); Vec(b, 12, 20, 30);
    }
    return b;
}

std::string WriteFixture(const std::vector<uint8_t>& bytes) {
    char path[] = "/tmp/mappack-spawn-flags-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    out.close();
    return path;
}

bool Loads(const std::vector<uint8_t>& bytes, MapSpecV2& spec) {
    const std::string path = WriteFixture(bytes);
    const bool loaded = LoadMapPackV2(path.c_str(), spec);
    std::remove(path.c_str());
    return loaded;
}

}  // namespace

int main(int argc, char** argv) {
    MapSpecV2 spec;
    assert(Loads(Fixture(2, 0), spec));
    assert(spec.rooms[0].spawns[0].actor_flags == 0);
    assert(spec.rooms[0].spawns[0].source_id == 17);
    ActorSpawn actors[2] = {};
    assert(spec.AllActorSpawns(actors, 2) == 2);
    assert(actors[0].placeholder_id == 3 && actors[0].actor_flags == 0);
    assert(actors[1].source_id == 18 && actors[1].actor_flags == 0);
    assert(spec.AllActorSpawns(actors, 1) == -1);

    assert(Loads(Fixture(3, 1), spec));
    assert(spec.rooms[0].spawns[0].actor_flags == 1);
    assert(spec.AllActorSpawns(actors, 2) == 2);
    assert(actors[0].actor_flags == 1 && actors[1].actor_flags == 0);

    assert(!Loads(Fixture(3, 0x80), spec));
    assert(!Loads(Fixture(3, 1, "PlayerSpawn"), spec));
    std::vector<uint8_t> truncated = Fixture(3, 1);
    truncated.pop_back();
    assert(!Loads(truncated, spec));

    assert(Loads(Fixture(4, 1), spec));
    assert(spec.hazard_count == 2);
    assert(spec.hazards[0].kind == kHazardDeathBlock);
    assert(spec.hazards[0].source_id == 51);
    assert(spec.hazards[0].bounds.min.x == 1.0f &&
           spec.hazards[0].bounds.max.z == 6.0f);
    assert(spec.hazards[1].kind == kHazardSpikeBlock);
    assert(spec.hazards[1].source_id == 52);
    assert(spec.hazards[1].spike_direction.x == 1.0f);

    std::vector<uint8_t> bad_spike = Fixture(4, 0);
    const size_t direction_x = bad_spike.size() - 12;
    // Replace unit spike direction X=1 with zero, creating invalid zero vector.
    bad_spike[direction_x + 0] = 0;
    bad_spike[direction_x + 1] = 0;
    bad_spike[direction_x + 2] = 0;
    assert(!Loads(bad_spike, spec));
    std::vector<uint8_t> truncated_hazard = Fixture(4, 0);
    truncated_hazard.pop_back();
    assert(!Loads(truncated_hazard, spec));

    assert(Loads(Fixture(5, 0), spec));
    assert(spec.dynamic_solid_count == 1);
    assert(std::strcmp(spec.dsol_path, "rom:/lvl/test/test.dsol") == 0);
    assert(spec.dynamic_solids[0].source_id == 100);
    assert(spec.dynamic_solids[0].solid_id == 198);
    assert(spec.dynamic_solids[0].kind == 1);
    assert(spec.dynamic_solids[0].cms_first_source_face_id == 41);
    assert(spec.dynamic_solids[0].target_source_id == 101);
    assert(spec.dynamic_solids[0].target_position.x == 12.0f);
    std::vector<uint8_t> malformed_v5 = Fixture(5, 0);
    malformed_v5[malformed_v5.size() - 53] = 0x80;  // unknown actor flag
    assert(!Loads(malformed_v5, spec));
    std::vector<uint8_t> bad_dsol_path = Fixture(5, 0);
    bad_dsol_path[bad_dsol_path.size() - 130] = 'x';
    assert(!Loads(bad_dsol_path, spec));
    assert(spec.dynamic_solid_count == 1);
    assert(std::strcmp(spec.dsol_path, "rom:/lvl/test/test.dsol") == 0);
    std::vector<uint8_t> truncated_v5 = Fixture(5, 0);
    truncated_v5.pop_back();
    assert(!Loads(truncated_v5, spec));

    if (argc > 1) {
        MapSpecV2 baked;
        assert(LoadMapPackV2(argv[1], baked));
        ActorSpawn baked_actors[ActorWorld::kMaxActors] = {};
        const int baked_count = baked.AllActorSpawns(
            baked_actors, ActorWorld::kMaxActors);
        assert(baked_count == 34);
        int strawberries = 0, refills = 0, double_refills = 0, springs = 0;
        for (int i = 0; i < baked_count; ++i) {
            if (baked_actors[i].placeholder_id == 2) ++strawberries;
            else if (baked_actors[i].placeholder_id == 3) {
                ++refills;
                if (baked_actors[i].actor_flags & 1) ++double_refills;
            } else if (baked_actors[i].placeholder_id == 7) ++springs;
            for (int j = i + 1; j < baked_count; ++j)
                assert(baked_actors[i].source_id != baked_actors[j].source_id);
        }
        assert(strawberries == 20 && refills == 8 && springs == 6);
        assert(double_refills == 3);
    }

    std::puts("PASS: mappack_spawn_flags_smoke");
}
