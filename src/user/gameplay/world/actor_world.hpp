#pragma once
#include <cstdint>
#include "gameplay/actor/actor.hpp"
namespace madeline_cube {
using ActorId = uint16_t;
class ActorWorld {
public:
    static constexpr uint16_t kMaxActors = 64;
    ActorId Add(Actor& actor);
    void Destroy(ActorId id);
    void ResolvePending();
    void Update(float delta_seconds);
    uint16_t Count() const { return count_; }
    // First active actor of EXACTLY type T, or null. Resolves by comparing
    // `type_id_` against `T::kTypeId` (Inc 4) instead of `dynamic_cast`, so
    // no RTTI is needed on the N64 hot path. Exact-type semantics: the old
    // `dynamic_cast` also matched derived types, but the only live query
    // targets a concrete leaf type (`StrawberryActor`), so the behavior is
    // unchanged there. `T` must be a concrete actor class carrying `kTypeId`.
    template<class T> T* Get() const {
        for (uint16_t i=0;i<kMaxActors;++i)
            if (actors_[i] && active_[i] && actors_[i]->TypeId() == T::kTypeId)
                return static_cast<T*>(actors_[i]);
        return nullptr;
    }
private:
    Actor* actors_[kMaxActors] = {};
    bool active_[kMaxActors] = {};
    bool pending_add_[kMaxActors] = {};
    bool pending_remove_[kMaxActors] = {};
    uint16_t count_ = 0;
};
}
