# Contracts — new and changed APIs

Only APIs that cross module boundaries. Internal helpers omitted.

## New: `physics/coll_mesh.hpp`

```cpp
namespace madeline_cube::physics {

struct CollMesh {                       // RDRAM-resident, immutable after load
    const CollHeader* header;           // points into the loaded blob
    const CollVertex* vertices;
    const CollTriangle* triangles;
    const CollBvhNode* bvh_nodes;
    const CollSurfaceLink* surface_links;  // sorted by face_id
};

// Load a `.colmesh` from DFS. Returns nullptr on bad magic/version.
// Owns the backing allocation; pair with FreeCollMesh.
const CollMesh* LoadCollMesh(const char* dfs_path);
void            FreeCollMesh(const CollMesh*);

struct RayHit {
    bool   hit = false;
    float  t = 0.0f;          // distance along ray
    Vec3   point;
    Vec3   normal;
    int    face_id = -1;
    uint16_t material = 0;
};

struct SphereHit {
    bool   hit = false;
    float  t = 0.0f;          // fraction along sweep, [0,1]
    Vec3   point;
    Vec3   normal;
    int    face_id = -1;
    uint16_t material = 0;
};

// All queries are read-only and reentrant.

RayHit    RaycastMesh    (const CollMesh&, const Vec3& origin, const Vec3& dir,
                           float max_distance, BackfacePolicy);
SphereHit SweepSphereMesh(const CollMesh&, const Vec3& origin, const Vec3& dir,
                           float radius, float max_distance);
int       OverlapAabbMesh(const CollMesh&, const AABB& query,
                           int* out_face_ids, int max_out);

// O(log N) over sorted SurfaceLink[].
constexpr uint16_t INVALID_OWNER = 0xFFFF;
uint16_t  SurfaceOwnerOf(const CollMesh&, int face_id);  // INVALID_OWNER if static.

}  // namespace
```

## Changed: `Room` in `gameplay/world/world.hpp`

```cpp
struct Room {
    // Legacy (kept through Inc 6, removed in Inc 7):
    static constexpr int kMaxColliders = 512;
    Collider colliders[kMaxColliders];
    int collider_count = 0;

    // New:
    const physics::CollMesh* coll_mesh = nullptr;
    bool use_collmesh = false;

    // Unchanged:
    MovingSurface moving_surfaces[/*…*/];
    // …existing fields…
};
```

## Unchanged: query return shapes

`GroundHit`, `WallHit`, `CeilingHit`, `BackfacePolicy` — exact same fields.
Inc 4 populates them from `SphereHit` / `RayHit` under the hood; callers see
no diff.

## Unchanged: motor boundary

`MotorInput`, `MotorResult`, `PlayerMotorConfig`, `PlayerMotor::Step`,
`PlayerMotor::RefreshContacts` — signatures unchanged. Implementation in Inc 6
swaps the substrate.

## New: timing in `gameplay/runtime/timing.hpp`

```cpp
namespace madeline_cube::runtime {

constexpr float kTickHz   = 60.0f;
constexpr float kTickDt   = 1.0f / kTickHz;
constexpr int   kMaxTicksPerFrame = 5;

struct TickAccumulator {
    float carry = 0.0f;     // unconsumed time
    // returns # of fixed steps to run this frame
    int Advance(float real_delta_seconds);
    float InterpAlpha() const { return carry / kTickDt; }
};

}  // namespace
```
