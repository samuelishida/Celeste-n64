#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

#include "gameplay/actor/actor.hpp"
#include "gameplay/world/actor_update_view.hpp"

namespace madeline_cube {
using ActorId = uint16_t;

class ActorWorld {
public:
    static constexpr uint16_t kMaxActors = 64;
    static constexpr size_t kOwnedSlotBytes = 80;

    ActorWorld() = default;
    ~ActorWorld() { Clear(); }
    ActorWorld(const ActorWorld&) = delete;
    ActorWorld& operator=(const ActorWorld&) = delete;
    ActorWorld(ActorWorld&&) = delete;
    ActorWorld& operator=(ActorWorld&&) = delete;

    // Non-owning registration, retained for stack-owned fixtures and actors.
    ActorId Add(Actor& actor);
    // Construct one stable actor in bounded inline storage. Returns null when
    // all 64 registry slots are occupied; no heap allocation occurs.
    template<class T, class... Args> T* EmplaceOwned(Args&&... args) {
        static_assert(std::is_base_of<Actor, T>::value,
                      "owned actor type must derive from Actor");
        static_assert(sizeof(T) <= kOwnedSlotBytes,
                      "actor exceeds ActorWorld inline slot budget");
        static_assert(alignof(T) <= alignof(std::max_align_t),
                      "actor exceeds ActorWorld inline alignment");
        for (uint16_t i = 0; i < kMaxActors; ++i) {
            if (actors_[i]) continue;
            T* actor = ::new (static_cast<void*>(owned_storage_[i]))
                T(std::forward<Args>(args)...);
            actors_[i] = actor;
            destroy_owned_[i] = [](Actor* base) {
                static_cast<T*>(base)->~T();
            };
            pending_add_[i] = true;
            return actor;
        }
        return nullptr;
    }
    void Destroy(ActorId id);
    void ResolvePending();
    void Update(float delta_seconds);
    void UpdateBefore(uint32_t source_id, float delta_seconds);
    void UpdateAfter(uint32_t source_id, float delta_seconds);
    void LateUpdate(float delta_seconds);
    void LateUpdateBefore(uint32_t source_id, float delta_seconds);
    void LateUpdateAfter(uint32_t source_id, float delta_seconds);
    // Snapshot point-actor insertion order once, then dispatch monotonically
    // up to each external source slot (dynamic Solid, Player, cassette).
    void BeginSourceOrderedPass(bool late_update,
                                const ActorUpdateView* update_view = nullptr);
    void DispatchSourceOrderedBefore(uint32_t source_id, float delta_seconds);
    // Dispatch one externally-owned actor at its canonical source slot.
    // Used for actors whose storage/lifetime remains scene-owned.
    void DispatchSourceOrderedActorSlot(Actor& actor, float delta_seconds);
    void DispatchSourceOrderedRemainder(float delta_seconds);
    void Clear();
    uint16_t Count() const { return count_; }
    ActorId IdOf(const Actor* actor) const {
        for (uint16_t i = 0; i < kMaxActors; ++i)
            if (actors_[i] == actor) return static_cast<ActorId>(i + 1);
        return 0;
    }
    // First resolved actor slot of EXACTLY type T, or null. Does not filter
    // the actor's gameplay `active` flag. Resolves by comparing `type_id_`
    // against `T::kTypeId` (Inc 4) instead of `dynamic_cast`, so
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
    // Stable slot-order traversal; includes behavior-inactive actors so
    // callers can apply distinct update, interaction, and visibility rules.
    template<class T, class Fn> void ForEachType(Fn&& fn) const {
        for (uint16_t i = 0; i < kMaxActors; ++i) {
            if (actors_[i] && active_[i] && actors_[i]->TypeId() == T::kTypeId)
                fn(*static_cast<T*>(actors_[i]));
        }
    }

private:
    void DispatchPhase(float delta_seconds, bool late_update,
                       bool source_limited, uint32_t source_id,
                       bool before_source);
    bool PassesUpdateView(const Actor* actor) const;

    Actor* actors_[kMaxActors] = {};
    using DestroyFn = void (*)(Actor*);
    DestroyFn destroy_owned_[kMaxActors] = {};
    alignas(std::max_align_t) unsigned char
        owned_storage_[kMaxActors][kOwnedSlotBytes] = {};
    bool active_[kMaxActors] = {};
    bool pending_add_[kMaxActors] = {};
    bool pending_remove_[kMaxActors] = {};
    uint16_t count_ = 0;
    uint16_t ordered_slots_[kMaxActors] = {};
    uint16_t ordered_count_ = 0;
    uint16_t ordered_cursor_ = 0;
    bool ordered_late_update_ = false;
    bool ordered_pass_active_ = false;
    ActorUpdateView ordered_update_view_{};
    bool ordered_view_active_ = false;
};
}
