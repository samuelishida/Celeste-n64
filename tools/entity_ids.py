"""Entity class ID enumeration (Python mirror of src/user/gameplay/world/entity_ids.hpp)."""

ENTITY_IDS = {
    "PlayerSpawn": 0,
    "Strawberry": 1,
    "Refill": 2,
    "Spring": 3,
    "Cassette": 9,
}

def id_of(classname):
    """Get the entity ID for a given classname."""
    if classname not in ENTITY_IDS:
        return None
    return ENTITY_IDS[classname]

def name_of(entity_id):
    """Get the classname for a given entity ID."""
    for name, eid in ENTITY_IDS.items():
        if eid == entity_id:
            return name
    return None
