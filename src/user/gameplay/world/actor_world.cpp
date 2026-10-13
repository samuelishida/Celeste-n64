#include "gameplay/world/actor_world.hpp"
namespace madeline_cube {
namespace {
uint16_t BuildSourceOrder(Actor* const* actors, const bool* active,
                          uint16_t (&ordered)[ActorWorld::kMaxActors]) {
    uint16_t count = 0;
    for (uint16_t slot = 0; slot < ActorWorld::kMaxActors; ++slot) {
        if (!actors[slot] || !active[slot]) continue;
        uint16_t position = count;
        while (position > 0) {
            const uint16_t previous = ordered[position - 1];
            if (actors[previous]->source_id <= actors[slot]->source_id) break;
            ordered[position] = previous;
            --position;
        }
        ordered[position] = slot;
        ++count;
    }
    return count;
}
}  // namespace

ActorId ActorWorld::Add(Actor& actor) {
    for (uint16_t i = 0; i < kMaxActors; ++i)
        if (actors_[i] == &actor) return 0;
    for (uint16_t i = 0; i < kMaxActors; ++i) {
        if (!actors_[i]) {
            actors_[i] = &actor;
            pending_add_[i] = true;
            return static_cast<ActorId>(i + 1);
        }
    }
    return 0;
}
void ActorWorld::Destroy(ActorId id) {
    if (id && id <= kMaxActors && actors_[id - 1]) pending_remove_[id - 1] = true;
}
void ActorWorld::ResolvePending() {
    for (uint16_t i = 0; i < kMaxActors; ++i) {
        if (pending_remove_[i]) {
            if (active_[i]) --count_;
            if (destroy_owned_[i]) destroy_owned_[i](actors_[i]);
            actors_[i] = nullptr;
            destroy_owned_[i] = nullptr;
            active_[i] = false;
            pending_add_[i] = false;
            pending_remove_[i] = false;
        } else if (pending_add_[i]) {
            active_[i] = true;
            pending_add_[i] = false;
            actors_[i]->Init();
            ++count_;
        }
    }
}
void ActorWorld::Update(float dt) {
    DispatchPhase(dt, false, false, 0, false);
}

void ActorWorld::UpdateBefore(uint32_t source_id, float dt) {
    DispatchPhase(dt, false, true, source_id, true);
}

void ActorWorld::UpdateAfter(uint32_t source_id, float dt) {
    DispatchPhase(dt, false, true, source_id, false);
}

void ActorWorld::LateUpdate(float dt) {
    DispatchPhase(dt, true, false, 0, false);
}

void ActorWorld::LateUpdateBefore(uint32_t source_id, float dt) {
    DispatchPhase(dt, true, true, source_id, true);
}

void ActorWorld::LateUpdateAfter(uint32_t source_id, float dt) {
    DispatchPhase(dt, true, true, source_id, false);
}

void ActorWorld::BeginSourceOrderedPass(bool late_update,
                                        const ActorUpdateView* update_view) {
    ordered_count_ = BuildSourceOrder(actors_, active_, ordered_slots_);
    ordered_cursor_ = 0;
    ordered_late_update_ = late_update;
    ordered_view_active_ = update_view != nullptr;
    if (update_view) ordered_update_view_ = *update_view;
    ordered_pass_active_ = true;
}

bool ActorWorld::PassesUpdateView(const Actor* actor) const {
    if (!ordered_view_active_ || actor->update_off_screen) return true;
    const Vec3 half = actor->UpdateBoundsHalfExtent();
    const Vec3 offset = actor->UpdateBoundsOffset();
    const Vec3 center{actor->position.x + offset.x,
                      actor->position.y + offset.y,
                      actor->position.z + offset.z};
    const Vec3 bounds_min{center.x - half.x, center.y - half.y,
                          center.z - half.z};
    const Vec3 bounds_max{center.x + half.x, center.y + half.y,
                          center.z + half.z};
    return ordered_update_view_.IntersectsBounds(bounds_min, bounds_max);
}

void ActorWorld::DispatchSourceOrderedBefore(uint32_t source_id, float dt) {
    if (!ordered_pass_active_) return;
    while (ordered_cursor_ < ordered_count_) {
        const uint16_t slot = ordered_slots_[ordered_cursor_];
        Actor* actor = actors_[slot];
        if (!actor || actor->source_id >= source_id) break;
        ++ordered_cursor_;
        if (!(actor->active || actor->UpdatesWhenInactive()) ||
            !PassesUpdateView(actor)) continue;
        if (ordered_late_update_) actor->LateUpdate(dt);
        else actor->Update(dt);
    }
}

void ActorWorld::DispatchSourceOrderedActorSlot(Actor& actor, float dt) {
    if (!ordered_pass_active_) return;
    DispatchSourceOrderedBefore(actor.source_id, dt);
    if (!(actor.active || actor.UpdatesWhenInactive()) ||
        !PassesUpdateView(&actor)) return;
    if (ordered_late_update_) actor.LateUpdate(dt);
    else actor.Update(dt);
}

void ActorWorld::DispatchSourceOrderedRemainder(float dt) {
    if (!ordered_pass_active_) return;
    while (ordered_cursor_ < ordered_count_) {
        Actor* actor = actors_[ordered_slots_[ordered_cursor_++]];
        if (!actor || !(actor->active || actor->UpdatesWhenInactive()) ||
            !PassesUpdateView(actor)) continue;
        if (ordered_late_update_) actor->LateUpdate(dt);
        else actor->Update(dt);
    }
    ordered_pass_active_ = false;
}

void ActorWorld::DispatchPhase(float dt, bool late_update,
                               bool source_limited, uint32_t source_id,
                               bool before_source) {
    uint16_t ordered[kMaxActors] = {};
    const uint16_t count = BuildSourceOrder(actors_, active_, ordered);
    for (uint16_t n = 0; n < count; ++n) {
        Actor* actor = actors_[ordered[n]];
        if (!actor || !(actor->active || actor->UpdatesWhenInactive())) continue;
        if (source_limited &&
            ((before_source && actor->source_id >= source_id) ||
             (!before_source && actor->source_id < source_id))) continue;
        if (late_update) actor->LateUpdate(dt);
        else actor->Update(dt);
    }
}

void ActorWorld::Clear() {
    for (uint16_t i = 0; i < kMaxActors; ++i) {
        if (destroy_owned_[i] && actors_[i]) destroy_owned_[i](actors_[i]);
        actors_[i] = nullptr;
        destroy_owned_[i] = nullptr;
        active_[i] = false;
        pending_add_[i] = false;
        pending_remove_[i] = false;
    }
    count_ = 0;
    ordered_count_ = ordered_cursor_ = 0;
    ordered_pass_active_ = false;
    ordered_view_active_ = false;
}
}
