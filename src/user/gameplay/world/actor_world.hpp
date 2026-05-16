#pragma once
#include <cstdint>
#include "gameplay/actor/actor.hpp"
namespace madeline_cube {
using ActorId = uint16_t;
template<class T> struct ActorView {
    T* items[64] = {};
    uint16_t count = 0;
    T* begin() const { return count ? items[0] : nullptr; }
};
class ActorWorld {
public:
    static constexpr uint16_t kMaxActors = 64;
    ActorId Add(Actor& actor);
    void Destroy(ActorId id);
    void ResolvePending();
    void Update(float delta_seconds);
    uint16_t Count() const { return count_; }
    template<class T> T* Get() const {
        for (uint16_t i=0;i<kMaxActors;++i) if (actors_[i] && active_[i]) if (auto* found=dynamic_cast<T*>(actors_[i])) return found;
        return nullptr;
    }
    template<class T> ActorView<T> All() const {
        ActorView<T> out;
        for (uint16_t i=0;i<kMaxActors;++i) if (actors_[i] && active_[i]) if (auto* found=dynamic_cast<T*>(actors_[i])) out.items[out.count++]=found;
        return out;
    }
    template<class T> T* OverlapsFirst(const Vec3& point) const {
        for (uint16_t i=0;i<kMaxActors;++i) if (actors_[i] && active_[i]) if (auto* found=dynamic_cast<T*>(actors_[i])) {
            const AABB world = WorldBounds(*actors_[i]); if (world.Contains(point)) return found;
        }
        return nullptr;
    }
private:
    static AABB WorldBounds(const Actor& actor) {
        return {
            {actor.position.x + actor.local_bounds.min.x, actor.position.y + actor.local_bounds.min.y, actor.position.z + actor.local_bounds.min.z},
            {actor.position.x + actor.local_bounds.max.x, actor.position.y + actor.local_bounds.max.y, actor.position.z + actor.local_bounds.max.z},
        };
    }
    Actor* actors_[kMaxActors] = {};
    bool active_[kMaxActors] = {};
    bool pending_add_[kMaxActors] = {};
    bool pending_remove_[kMaxActors] = {};
    uint16_t count_ = 0;
};
}
