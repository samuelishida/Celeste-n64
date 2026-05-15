#include "placeholder_catalog.hpp"

namespace madeline_cube {

namespace {

// Static catalog table. IDs are dense and ordered for O(1) lookup.
// Names follow gameplay meaning, not source filenames.
constexpr PlaceholderAsset kCatalog[] = {
    // Player
    {1, SemanticKind::Player, PrimitiveKind::Capsule, 1, 1},

    // Pickups
    {2, SemanticKind::PickupStrawberry, PrimitiveKind::Sphere, 2, 2},
    {3, SemanticKind::PickupRefill, PrimitiveKind::Octahedron, 3, 1},
    {4, SemanticKind::PickupCoin, PrimitiveKind::Cylinder, 4, 1},
    {5, SemanticKind::PickupFeather, PrimitiveKind::Quad, 5, 2},
    {6, SemanticKind::PickupCassette, PrimitiveKind::Box, 6, 1},

    // Actors / hazards
    {7, SemanticKind::ActorSpring, PrimitiveKind::Box, 7, 1},
    {8, SemanticKind::HazardSpike, PrimitiveKind::Cone, 8, 1},

    // Solids (color-coded by behavior variant)
    {9, SemanticKind::SolidBlock, PrimitiveKind::Box, 9, 1},
    {10, SemanticKind::SolidBlock, PrimitiveKind::Box, 10, 1},
    {11, SemanticKind::SolidBlock, PrimitiveKind::Box, 11, 1},
    {12, SemanticKind::SolidBlock, PrimitiveKind::Box, 12, 1},

    // NPCs (distinct colored capsules)
    {13, SemanticKind::NpcGranny, PrimitiveKind::Capsule, 13, 2},
    {14, SemanticKind::NpcBadeline, PrimitiveKind::Capsule, 14, 2},
    {15, SemanticKind::NpcTheo, PrimitiveKind::Capsule, 15, 2},

    // Interactables
    {16, SemanticKind::InteractSign, PrimitiveKind::Quad, 16, 1},

    // Decor
    {17, SemanticKind::DecorFoliage, PrimitiveKind::Cone, 17, 1},
    {18, SemanticKind::DecorFoliage, PrimitiveKind::Sphere, 17, 2},
    {19, SemanticKind::DecorProp, PrimitiveKind::Box, 18, 1},
    {20, SemanticKind::DecorProp, PrimitiveKind::Cylinder, 18, 1},

    // Props
    {21, SemanticKind::PropIntroCar, PrimitiveKind::Box, 19, 2},
    {22, SemanticKind::PropIntroCar, PrimitiveKind::Cylinder, 19, 1},

    // Room geometry
    {23, SemanticKind::RoomGeometry, PrimitiveKind::Box, 20, 3},
    {24, SemanticKind::RoomGeometry, PrimitiveKind::Wedge, 20, 2},
    {25, SemanticKind::RoomGeometry, PrimitiveKind::Quad, 21, 3},
};

constexpr uint16_t kCatalogCount = sizeof(kCatalog) / sizeof(kCatalog[0]);

}  // namespace

uint16_t PlaceholderCatalog::Count() {
    return kCatalogCount;
}

const PlaceholderAsset* PlaceholderCatalog::Lookup(uint16_t placeholder_id) {
    // IDs are 1-based in the table; index 0 is unused.
    if (placeholder_id == 0 || placeholder_id > kCatalogCount) {
        return nullptr;
    }
    return &kCatalog[placeholder_id - 1];
}

const PlaceholderAsset* PlaceholderCatalog::FindBySemantic(SemanticKind semantic) {
    for (uint16_t i = 0; i < kCatalogCount; ++i) {
        if (kCatalog[i].semantic == semantic) {
            return &kCatalog[i];
        }
    }
    return nullptr;
}

const char* PlaceholderCatalog::SemanticName(SemanticKind semantic) {
    switch (semantic) {
        case SemanticKind::None: return "none";
        case SemanticKind::Player: return "player";
        case SemanticKind::PickupStrawberry: return "pickup_strawberry";
        case SemanticKind::PickupRefill: return "pickup_refill";
        case SemanticKind::PickupCoin: return "pickup_coin";
        case SemanticKind::PickupFeather: return "pickup_feather";
        case SemanticKind::PickupCassette: return "pickup_cassette";
        case SemanticKind::ActorSpring: return "actor_spring";
        case SemanticKind::HazardSpike: return "hazard_spike";
        case SemanticKind::SolidBlock: return "solid_block";
        case SemanticKind::NpcGranny: return "npc_granny";
        case SemanticKind::NpcBadeline: return "npc_badeline";
        case SemanticKind::NpcTheo: return "npc_theo";
        case SemanticKind::InteractSign: return "interact_sign";
        case SemanticKind::DecorFoliage: return "decor_foliage";
        case SemanticKind::DecorProp: return "decor_prop";
        case SemanticKind::PropIntroCar: return "prop_intro_car";
        case SemanticKind::RoomGeometry: return "room_geometry";
    }
    return "unknown";
}

const char* PlaceholderCatalog::PrimitiveName(PrimitiveKind primitive) {
    switch (primitive) {
        case PrimitiveKind::None: return "none";
        case PrimitiveKind::Box: return "box";
        case PrimitiveKind::Sphere: return "sphere";
        case PrimitiveKind::Capsule: return "capsule";
        case PrimitiveKind::Cone: return "cone";
        case PrimitiveKind::Quad: return "quad";
        case PrimitiveKind::Cylinder: return "cylinder";
        case PrimitiveKind::Octahedron: return "octahedron";
        case PrimitiveKind::Wedge: return "wedge";
    }
    return "unknown";
}

}  // namespace madeline_cube
