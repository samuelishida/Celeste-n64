#include <cassert>
#include <cstdint>

#include "gameplay/world/dynamic_solid_geometry.hpp"

using namespace madeline_cube;

int main() {
    const DynamicSolidFace faces[] = {
        {.material_id = 2, .first_vertex = 0, .vertex_count = 3},
        {.material_id = 2, .first_vertex = 3, .vertex_count = 4},
        {.material_id = 3, .first_vertex = 7, .vertex_count = 3},
        {.material_id = 3, .first_vertex = 10, .vertex_count = 4},
        {.material_id = 3, .first_vertex = 15, .vertex_count = 3},  // gap
    };

    DynamicSolidFaceBatch batch = BuildDynamicSolidFaceBatch(faces, 0, 5, 70);
    assert(batch.valid && batch.end_face == 2);
    assert(batch.first_vertex == 0 && batch.vertex_count == 8);

    batch = BuildDynamicSolidFaceBatch(faces, 2, 5, 70);
    assert(batch.valid && batch.end_face == 4);
    assert(batch.first_vertex == 6 && batch.vertex_count == 8);

    batch = BuildDynamicSolidFaceBatch(faces, 4, 5, 70);
    assert(batch.valid && batch.end_face == 5);
    assert(batch.first_vertex == 14 && batch.vertex_count == 4);

    const DynamicSolidFace capacity_faces[] = {
        {.material_id = 1, .first_vertex = 1, .vertex_count = 68},
        {.material_id = 1, .first_vertex = 69, .vertex_count = 3},
    };
    batch = BuildDynamicSolidFaceBatch(capacity_faces, 0, 2, 70);
    assert(batch.valid && batch.end_face == 1);
    assert(batch.first_vertex == 0 && batch.vertex_count == 70);
    batch = BuildDynamicSolidFaceBatch(capacity_faces, 1, 2, 70);
    assert(batch.valid && batch.first_vertex == 68 && batch.vertex_count == 4);
    assert(!BuildDynamicSolidFaceBatch(capacity_faces, 0, 2, 69).valid);
    assert(!BuildDynamicSolidFaceBatch(faces, 5, 5, 70).valid);
}
