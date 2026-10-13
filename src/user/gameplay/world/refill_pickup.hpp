#pragma once

#include "gameplay/actor/refill_actor.hpp"
#include "gameplay/player/player_controller.hpp"
#include "gameplay/world/actor_world.hpp"

namespace madeline_cube {

// OG Player.Update tests its waist point against every pickup with strict
// sphere overlap. Refill side effects happen only after dash count changes.
inline int ProcessRefillPickups(ActorWorld& actors,
                                const PlayerController& player_controller,
                                PlayerState& player,
                                float player_half_height,
                                float waist_offset) {
    const Vec3 waist = {
        player.position.x,
        player.position.y - player_half_height + waist_offset,
        player.position.z,
    };
    int collected = 0;
    actors.ForEachType<RefillActor>([&](RefillActor& refill) {
        if (!refill.CanPickup(player.dashes) ||
            !refill.OverlapsPickupPoint(waist)) {
            return;
        }
        if (!player_controller.RefillDash(player, refill.DashAmount())) return;

        refill.OnCollect();
        player_controller.RequestWorldHitstop(player, 0.05f);
        ++collected;
    });
    return collected;
}

}  // namespace madeline_cube
