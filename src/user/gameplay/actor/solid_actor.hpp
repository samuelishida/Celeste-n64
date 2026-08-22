#pragma once
#include "gameplay/actor/actor.hpp"
#include "gameplay/world/world.hpp"
namespace madeline_cube {
class SolidActor : public Actor {
public:
    static constexpr uint16_t kTypeId = static_cast<uint16_t>(ActorTypeId::kSolid);
    SolidActor() { type_id_ = kTypeId; }

    static constexpr int kMaxFaces = 6;
    int owner_id = -1;
    Collider faces[kMaxFaces];
    int face_count = 0;
    void AddFace(const Collider& face);
    void SyncToRoom(Room& room) const;
};
}
