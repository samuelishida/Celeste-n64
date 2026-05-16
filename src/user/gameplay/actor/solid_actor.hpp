#pragma once
#include "gameplay/actor/actor.hpp"
#include "gameplay/world/world.hpp"
namespace madeline_cube {
class SolidActor : public Actor {
public:
    static constexpr int kMaxFaces = 6;
    int owner_id = -1;
    Collider faces[kMaxFaces];
    int face_count = 0;
    void AddFace(const Collider& face);
    void SyncToRoom(Room& room) const;
};
}
