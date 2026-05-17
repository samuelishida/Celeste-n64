#include "gameplay/world/actor_world.hpp"
namespace madeline_cube {
ActorId ActorWorld::Add(Actor& actor) {
    for (uint16_t i=0;i<kMaxActors;++i) if (!actors_[i]) { actors_[i]=&actor; pending_add_[i]=true; return i+1; }
    return 0;
}
void ActorWorld::Destroy(ActorId id) { if (id && id<=kMaxActors && actors_[id-1]) pending_remove_[id-1]=true; }
void ActorWorld::ResolvePending() {
    for (uint16_t i=0;i<kMaxActors;++i) {
        if (pending_remove_[i]) { if (active_[i]) { --count_; } actors_[i]=nullptr; active_[i]=false; pending_add_[i]=false; pending_remove_[i]=false; }
        else if (pending_add_[i]) { active_[i]=true; pending_add_[i]=false; actors_[i]->Init(); ++count_; }
    }
}
void ActorWorld::Update(float dt) { for (uint16_t i=0;i<kMaxActors;++i) if (actors_[i] && active_[i] && actors_[i]->active) actors_[i]->Update(dt); }
}
