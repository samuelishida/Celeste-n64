#pragma once

#include <cstdint>

namespace madeline_cube {

// Semantic kinds name what an asset means in gameplay.
enum class SemanticKind : uint8_t {
    None = 0,
    Player,
    PickupStrawberry,
    PickupRefill,
    PickupCoin,
    PickupFeather,
    PickupCassette,
    ActorSpring,
    HazardSpike,
    SolidBlock,
    NpcGranny,
    NpcBadeline,
    NpcTheo,
    InteractSign,
    DecorFoliage,
    DecorProp,
    PropIntroCar,
    RoomGeometry,
};

// Primitive kinds are the stand-in geometry used before final art conversion.
enum class PrimitiveKind : uint8_t {
    None = 0,
    Box,
    Sphere,
    Capsule,
    Cone,
    Quad,
    Cylinder,
    Octahedron,
    Wedge,
};

// A placeholder asset binds a semantic role to a primitive shape, color, and scale.
struct PlaceholderAsset {
    uint16_t placeholder_id = 0;
    SemanticKind semantic = SemanticKind::None;
    PrimitiveKind primitive = PrimitiveKind::None;
    uint16_t color_id = 0;
    uint16_t scale_id = 0;
};

// Catalog of all placeholder assets known to the prototype.
// This is a static table compiled into the ROM; lookups are O(1) via dense IDs.
class PlaceholderCatalog {
public:
    // Returns the number of entries in the catalog.
    static uint16_t Count();

    // Lookup by dense placeholder_id. Returns nullptr if out of range.
    static const PlaceholderAsset* Lookup(uint16_t placeholder_id);

    // Lookup by semantic kind. Returns the first match or nullptr.
    static const PlaceholderAsset* FindBySemantic(SemanticKind semantic);

    // Returns a human-readable name for a semantic kind.
    static const char* SemanticName(SemanticKind semantic);

    // Returns a human-readable name for a primitive kind.
    static const char* PrimitiveName(PrimitiveKind primitive);
};

}  // namespace madeline_cube
