#include "gameplay/world/entity_dispatch.hpp"
#include "gameplay/world/actor_factory.hpp"
#include "gameplay/world/entity_ids.hpp"

#ifdef __mips__
#include <libdragon.h>
#define ED_LOG debugf
#else
#include <cstdio>
#define ED_LOG printf
#endif

namespace madeline_cube {

namespace {

// Maps LVL classname_id → placeholder_id used by SpawnActor.
// ENT_STRAWBERRY=1 → placeholder 2 (PickupStrawberry)
// ENT_REFILL=2     → placeholder 3 (PickupRefill)
// ENT_SPRING=3     → placeholder 7 (ActorSpring)
static constexpr uint16_t kUnknown = 0;
static uint16_t ClassnameToPlaceholder(uint16_t classname_id) {
    switch (classname_id) {
        case kEntStrawberry: return 2;
        case kEntRefill: return 3;
        case kEntSpring: return 7;
        default: return kUnknown;
    }
}

}  // namespace

int DispatchActorSpawns(const ActorSpawn* spawns, int spawn_count,
                        ActorWorld& world) {
    if (!spawns || spawn_count <= 0) return 0;
    int dispatched = 0;
    for (int i = 0; i < spawn_count; ++i) {
        const ActorSpawn& spawn = spawns[i];
        if (spawn.placeholder_id != 2 && spawn.placeholder_id != 3 &&
            spawn.placeholder_id != 7) {
            ED_LOG("[dispatch] unknown placeholder=%u source=%lu — skipped\n",
                   spawn.placeholder_id, static_cast<unsigned long>(spawn.source_id));
            continue;
        }
        const ActorId id = SpawnActor(world, spawn);
        if (!id) {
            ED_LOG("[dispatch] spawn FAILED placeholder=%u source=%lu (capacity/flags)\n",
                   spawn.placeholder_id, static_cast<unsigned long>(spawn.source_id));
            continue;
        }
        ++dispatched;
    }
    return dispatched;
}

int DispatchLevelEntities(const Room& room, ActorWorld& world) {
    if (room.spawn_count > Room::kMaxSpawns) {
        ED_LOG("[dispatch] room spawn_count=%d exceeds cap=%d — aborted\n",
               room.spawn_count, Room::kMaxSpawns);
        return 0;
    }
    ActorSpawn translated_spawns[Room::kMaxSpawns];
    int translated_count = 0;
    for (int i = 0; i < room.spawn_count; ++i) {
        ActorSpawn translated = room.spawns[i];
        translated.placeholder_id = ClassnameToPlaceholder(room.spawns[i].placeholder_id);

        if (translated.placeholder_id == kUnknown) {
            ED_LOG("[dispatch] unknown classname_id=%u at (%.2f,%.2f,%.2f) — skipped\n",
                   room.spawns[i].placeholder_id,
                   static_cast<double>(translated.position.x),
                   static_cast<double>(translated.position.y),
                   static_cast<double>(translated.position.z));
            continue;
        }
        translated_spawns[translated_count++] = translated;
    }
    return DispatchActorSpawns(translated_spawns, translated_count, world);
}

}  // namespace madeline_cube
