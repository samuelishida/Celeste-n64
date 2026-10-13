#pragma once

#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Stable, unique type ids for actor classes (n64-optimization Inc 4).
// `ActorWorld::Get<T>()` resolves by exact `type_id_` comparison against
// `T::kTypeId` instead of `dynamic_cast`, so every concrete actor class
// carries its own constant and a collision would silently mis-resolve the
// query. The uniqueness contract is pinned by `tests/actor_type_id_smoke.cpp`.
enum class ActorTypeId : uint16_t {
    kActor = 0,
    kBobbing = 1,
    kStrawberry = 2,
    kRefill = 3,
    kSpring = 4,
    kSolid = 5,
    kMovingSolid = 6,
    kSpike = 7,
    kCassette = 8,
};

class Actor {
public:
    virtual ~Actor() = default;

    virtual void Init() {}
    virtual void Update(float delta_seconds) {}
    // OG World dispatches every actor Update before beginning its second
    // insertion-ordered LateUpdate pass. Actors without late work keep default.
    virtual void LateUpdate(float delta_seconds) { (void)delta_seconds; }
    virtual void OnCollect() {}  // Called when player touches this actor
    virtual bool IsCollectible() const { return false; }
    // Some hidden actors (notably refills) still own cooldown timers.
    virtual bool UpdatesWhenInactive() const { return false; }
    // Source Actor.LocalBounds as center offset + half extents. Virtual
    // metadata keeps the bounded 80-byte inline actor slots compact.
    virtual Vec3 UpdateBoundsOffset() const { return {}; }
    virtual Vec3 UpdateBoundsHalfExtent() const { return {}; }

    // Exact type of this actor instance, set by each concrete class's
    // constructor. Non-virtual on purpose: the id is fixed at construction
    // and `ActorWorld::Get<T>()` compares it directly (no RTTI).
    uint16_t TypeId() const { return type_id_; }

    Vec3 position{};
    bool update_off_screen = false;
    bool active = true;
    bool collected = false;
    float pickup_radius = 1.0f;
    uint16_t placeholder_id = 0;
    uint32_t source_id = 0;  // stable source entity id (Inc 9)

protected:
    // Set by the base constructor and overridden by each concrete actor
    // class's constructor. Protected so derived classes can stamp their own
    // `kTypeId` without exposing a public setter.
    uint16_t type_id_ = static_cast<uint16_t>(ActorTypeId::kActor);
};

}  // namespace madeline_cube
