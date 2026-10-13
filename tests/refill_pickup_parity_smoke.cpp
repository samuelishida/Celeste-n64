#include <cassert>
#include <cmath>

#include "gameplay/actor/refill_actor.hpp"
#include "gameplay/world/actor_world.hpp"
#include "gameplay/world/refill_pickup.hpp"
#include "gameplay/input/input_system.hpp"

using namespace madeline_cube;

int main() {
    RefillActor refill;
    refill.position = {4.0f, 8.0f, 12.0f};
    refill.Init();

    // OG Refill.PickupRadius is 20 world units, measured from SolidWaistTestPos.
    assert(refill.pickup_radius == 20.0f);

    const Vec3 anchor = refill.position;
    refill.Update(0.1f);
    assert(refill.position.x == anchor.x && refill.position.y == anchor.y &&
           refill.position.z == anchor.z);  // bob never moves pickup center
    const Vec3 visual = refill.VisualPosition();
    assert(std::fabs(visual.y -
        (anchor.y + std::sin(0.2f) * RefillActor::kVisualBobAmplitude)) < 1.0e-5f);
    assert(std::fabs(refill.VisualSpinYawRadians() - 0.3f) < 1.0e-5f);
    assert(refill.OverlapsPickupPoint({anchor.x + 19.999f, anchor.y, anchor.z}));
    assert(!refill.OverlapsPickupPoint({anchor.x + 20.0f, anchor.y, anchor.z}));
    assert(!refill.OverlapsPickupPoint({anchor.x + 12.0f, anchor.y, anchor.z + 16.0f}));
    assert(refill.CanPickup(0));
    assert(!refill.CanPickup(1));

    ActorWorld actors;
    RefillActor* single = actors.EmplaceOwned<RefillActor>();
    RefillActor* doubled = actors.EmplaceOwned<RefillActor>();
    assert(single && doubled && single != doubled);
    single->position = {0.0f, 0.0f, 0.0f};
    doubled->position = {100.0f, 0.0f, 0.0f};
    doubled->SetDouble(true);
    actors.ResolvePending();

    PlayerController controller;
    PlayerState player;
    player.dashes = 0;
    player.position = {19.999f, 8.25f, 0.0f};  // waist = feet + 3 = (19.999,0,0)
    assert(ProcessRefillPickups(actors, controller, player, 11.25f, 3.0f) == 1);
    assert(player.dashes == 1);
    assert(single->collected && !single->active);
    assert(std::fabs(player.dash_refill_flash_remaining - 0.05f) < 1.0e-6f);
    assert(player.world_hitstop_request == 0.05f);

    // One-charge refill is ineligible at one dash; double refill grants two
    // only when player stands within its own strict waist-radius sphere.
    PlayerState full_single;
    full_single.dashes = 1;
    full_single.position = {0.0f, 8.25f, 0.0f};
    assert(ProcessRefillPickups(actors, controller, full_single, 11.25f, 3.0f) == 0);
    assert(single->collected && !single->active);
    PlayerState double_pickup;
    double_pickup.dashes = 1;
    double_pickup.position = {100.0f, 8.25f, 0.0f};
    assert(doubled->CanPickup(1));
    assert(!doubled->CanPickup(2));
    assert(ProcessRefillPickups(actors, controller, double_pickup, 11.25f, 3.0f) == 1);
    assert(double_pickup.dashes == 2);
    assert(doubled->collected && !doubled->active);

    // Dormant cooldown advances for 4 seconds, but not before; then actor
    // returns and can be collected again.
    actors.Update(3.99f);
    assert(!single->active && single->collected);
    assert(std::fabs(single->CooldownRemaining() - 0.01f) < 1.0e-4f);
    actors.Update(0.02f);
    assert(single->active && !single->collected);

    // Ordered hitstop requests: refill's later .05 s assignment replaces
    // dash's .02 s assignment. Whole fixed ticks freeze every world consumer.
    PlayerState hitstop;
    controller.RequestWorldHitstop(hitstop, 0.02f);
    controller.RequestWorldHitstop(hitstop, 0.05f);
    controller.CommitWorldHitstopRequest(hitstop);
    assert(std::fabs(hitstop.world_hitstop_remaining - 0.05f) < 1.0e-6f);
    assert(hitstop.world_hitstop_request == 0.0f);

    InputSystem input;
    input.ObserveGameplayButtons(true, false, false);
    int paused_ticks = 0;
    int player_ticks = 0;
    int camera_ticks = 0;
    int actor_ticks = 0;
    float active_simulation_time = 0.0f;
    constexpr float dt = 1.0f / 60.0f;
    for (int tick = 0; tick < 4; ++tick) {
        if (controller.ConsumeWorldHitstopTick(hitstop, dt)) {
            ++paused_ticks;
            continue;
        }
        input.BeginTick(dt);
        active_simulation_time += dt;
        ++player_ticks;
        ++camera_ticks;
        ++actor_ticks;
    }
    assert(paused_ticks == 3);
    assert(player_ticks == 1 && camera_ticks == 1 && actor_ticks == 1);
    assert(std::fabs(active_simulation_time - dt) < 1.0e-6f);
    assert(input.Jump().pressed);  // raw edge survives pause; buffer starts afterward

    return 0;
}
