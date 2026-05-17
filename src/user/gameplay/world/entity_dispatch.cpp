#include "gameplay/world/entity_dispatch.hpp"
#include "gameplay/world/actor_factory.hpp"

#include <libdragon.h>

namespace madeline_cube {

namespace {

// Maps LVL classname_id → placeholder_id used by SpawnActor.
// ENT_STRAWBERRY=1 → placeholder 2 (PickupStrawberry)
// ENT_REFILL=2     → placeholder 3 (PickupRefill)
// ENT_SPRING=3     → placeholder 7 (ActorSpring)
static constexpr uint16_t kUnknown = 0;
static uint16_t ClassnameToPlaceholder(uint16_t classname_id) {
    switch (classname_id) {
        case 1: return 2;
        case 2: return 3;
        case 3: return 7;
        default: return kUnknown;
    }
}

}  // namespace

void DispatchLevelEntities(const Room& room, ActorWorld& world,
                           StrawberryActor& strawberry,
                           RefillActor& refill,
                           SpringActor& spring) {
    for (int i = 0; i < room.spawn_count; ++i) {
        ActorSpawn translated = room.spawns[i];
        translated.placeholder_id = ClassnameToPlaceholder(room.spawns[i].placeholder_id);

        if (translated.placeholder_id == kUnknown) {
            debugf("[dispatch] unknown classname_id=%u at (%.2f,%.2f,%.2f) — skipped\n",
                   room.spawns[i].placeholder_id,
                   static_cast<double>(translated.position.x),
                   static_cast<double>(translated.position.y),
                   static_cast<double>(translated.position.z));
            continue;
        }

        SpawnActor(world, translated, strawberry, refill, spring);
        debugf("[dispatch] spawned classname_id=%u ph=%u at (%.2f,%.2f,%.2f)\n",
               room.spawns[i].placeholder_id, translated.placeholder_id,
               static_cast<double>(translated.position.x),
               static_cast<double>(translated.position.y),
               static_cast<double>(translated.position.z));
    }
}

}  // namespace madeline_cube
