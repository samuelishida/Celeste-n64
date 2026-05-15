#pragma once

#include <cstdint>

#include "math_types.hpp"

namespace madeline_cube {

class Actor {
public:
    virtual ~Actor() = default;

    virtual void Init() {}
    virtual void Update(float delta_seconds) {}
    virtual void OnCollect() {}  // Called when player touches this actor
    virtual bool IsCollectible() const { return false; }

    Vec3 position{};
    bool active = true;
    bool collected = false;
    float pickup_radius = 1.0f;
    uint16_t placeholder_id = 0;
};

}  // namespace madeline_cube
