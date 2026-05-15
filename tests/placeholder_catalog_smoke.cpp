#include <cassert>
#include <cstdio>
#include <string>

#include "placeholder_catalog.hpp"

using namespace madeline_cube;

int main() {
    // Count should match the number of entries in the static table.
    assert(PlaceholderCatalog::Count() == 25);

    // Lookup valid IDs
    const PlaceholderAsset* player = PlaceholderCatalog::Lookup(1);
    assert(player != nullptr);
    assert(player->semantic == SemanticKind::Player);
    assert(player->primitive == PrimitiveKind::Capsule);

    const PlaceholderAsset* berry = PlaceholderCatalog::Lookup(2);
    assert(berry != nullptr);
    assert(berry->semantic == SemanticKind::PickupStrawberry);
    assert(berry->primitive == PrimitiveKind::Sphere);

    const PlaceholderAsset* refill = PlaceholderCatalog::Lookup(3);
    assert(refill != nullptr);
    assert(refill->semantic == SemanticKind::PickupRefill);
    assert(refill->primitive == PrimitiveKind::Octahedron);

    // Lookup invalid ID
    assert(PlaceholderCatalog::Lookup(0) == nullptr);
    assert(PlaceholderCatalog::Lookup(100) == nullptr);

    // Find by semantic
    const PlaceholderAsset* spring = PlaceholderCatalog::FindBySemantic(SemanticKind::ActorSpring);
    assert(spring != nullptr);
    assert(spring->primitive == PrimitiveKind::Box);

    const PlaceholderAsset* none = PlaceholderCatalog::FindBySemantic(SemanticKind::None);
    assert(none == nullptr);

    // Name lookups
    assert(std::string(PlaceholderCatalog::SemanticName(SemanticKind::Player)) == "player");
    assert(std::string(PlaceholderCatalog::SemanticName(SemanticKind::PickupStrawberry)) == "pickup_strawberry");
    assert(std::string(PlaceholderCatalog::PrimitiveName(PrimitiveKind::Box)) == "box");
    assert(std::string(PlaceholderCatalog::PrimitiveName(PrimitiveKind::Octahedron)) == "octahedron");

    printf("placeholder_catalog_smoke: all checks passed\n");
    return 0;
}
