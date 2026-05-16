#include "gameplay/actor/solid_actor.hpp"
namespace madeline_cube {
void SolidActor::AddFace(const Collider& face) {
    if (face_count >= kMaxFaces) return;
    faces[face_count] = face;
    faces[face_count].owner_id = owner_id;
    ++face_count;
}
void SolidActor::SyncToRoom(Room& room) const {
 for(int i=0;i<face_count && room.collider_count<Room::kMaxColliders;++i){ Collider c=faces[i]; c.owner_id=owner_id; room.colliders[room.collider_count++]=c; }
}
}
