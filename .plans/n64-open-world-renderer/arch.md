
# N64 Seamless Open-World Renderer — Technical Specification

## 1. Purpose

This document specifies an N64 rendering architecture capable of presenting a large, apparently seamless open world despite the Nintendo 64's severe depth-buffer, memory, bandwidth, and geometry limitations.

The design is based on the techniques demonstrated in James Lambert's `n64brew2025` overworld renderer.

Reference implementation:

* Repository: `lambertjamesd/n64brew2025`
* Main renderer: `src/overworld/overworld_render.c`
* Material system: `src/render/material.c`
* Overworld loading: `src/overworld/overworld_load.c`

The central design principle is:

> Do not attempt to render the entire world using one depth-tested coordinate space.

Instead, divide the visible world into rendering domains with different precision requirements.

```text
                    CAMERA
                       │
                       ▼
              ┌─────────────────┐
              │ Visibility / LOD│
              │ determination   │
              └────────┬────────┘
                       │
          ┌────────────┴────────────┐
          │                         │
          ▼                         ▼
   DISTANT WORLD               NEAR WORLD
   LOD / background            detailed tiles
   Z disabled                  Z enabled
   scaled coordinate           normal coordinate
   manual ordering             hardware Z test
          │                         │
          └────────────┬────────────┘
                       ▼
                 FINAL FRAME
```

---

# 2. N64 Constraints

## 2.1 The fundamental problem

A conventional open-world renderer wants:

```text
near clipping plane ───────────────────────────────► far clipping plane
       high precision                         low precision
```

However, perspective depth buffering allocates depth precision non-linearly.

The result is that increasing the far plane while retaining a very small near plane produces increasingly poor depth precision at long distances.

Eventually:

```text
Object A depth ≈ Object B depth
```

and the RDP cannot reliably determine which surface should be visible.

This manifests as:

* Z-fighting
* flickering terrain
* unstable distant geometry
* incorrect surface ordering
* visible artifacts when the camera moves

The solution is therefore not simply:

```text
increase far clip
```

but:

```text
change the representation of distant geometry
```

---

# 3. Rendering Architecture

The renderer consists of two principal world passes.

## Pass A — Distant World

Purpose:

Render terrain and environmental geometry that is too far away to require accurate depth testing.

Characteristics:

* Z-buffer disabled
* reduced world coordinate scale
* aggressively reduced geometry
* LOD hierarchy
* manual back-to-front ordering
* directional mesh selection
* aggressive culling
* pre-generated RSPQ blocks

The actual implementation explicitly switches the Z buffer off:

```c
rdpq_sync_pipe();
rdpq_mode_zbuf(false, false);
```

and restores it afterward:

```c
rdpq_sync_pipe();
rdpq_mode_zbuf(true, true);
```

This is implemented in `overworld_render_lod_1()`.

---

## Pass B — Near World

Purpose:

Render the detailed environment around the player.

Characteristics:

* Z-buffer enabled
* higher geometry density
* accurate depth testing
* multiple vertical terrain layers
* dynamic objects/particles
* normal material rendering

The near-world pass is performed after the distant LOD pass.

The renderer explicitly synchronizes and enables Z-buffering before rendering the detailed tiles:

```c
rdpq_sync_pipe();
rdpq_mode_zbuf(true, true);
```

---

# 4. Coordinate-System Strategy

The key trick is that the distant world does not need to occupy the same numerical coordinate range as the near world.

Let:

```text
W = original world coordinate
S = distant-world scale
D = W × S
```

where:

```text
S << 1
```

The distant representation compresses the entire world into a much smaller coordinate space.

In the implementation:

```c
float lod_scale = 1.0f / overworld->tile_x;
```

The camera translation is also multiplied by this scale:

```c
-camera->transform.position.x * lod_scale * WORLD_SCALE
-camera->transform.position.y * lod_scale * WORLD_SCALE
-camera->transform.position.z * lod_scale * WORLD_SCALE
```

This means that a large world can be represented inside a much smaller numerical coordinate range.

---

# 5. Distant Camera

The distant pass uses a separate camera configuration.

Conceptually:

```text
near_camera
    near = normal near clip
    far  = normal gameplay far clip

lod_camera
    near = scaled near clip
    far  = world/tile scale
```

The implementation derives:

```c
camera_t lod_1_camera = *camera;

lod_1_camera.near =
    (camera->far * 0.25f) * lod_scale;

lod_1_camera.far =
    overworld->tile_size * 1.4f;
```

This is important.

The distant pass is not merely using the original camera with Z disabled. It creates a separate projection/viewport configuration appropriate for the compressed world.

---

# 6. Distant World Transform

The distant world uses a scale transform.

Conceptually:

```text
                WORLD
                  │
                  │ × lod_scale
                  ▼
          COMPRESSED WORLD
                  │
                  ▼
               N64 RSP
```

The transformation contains:

```c
t3d_mat4_translate(
    &mtx,
    -camera->transform.position.x * lod_scale * WORLD_SCALE,
    -camera->transform.position.y * lod_scale * WORLD_SCALE,
    -camera->transform.position.z * lod_scale * WORLD_SCALE
);
```

Then the world axes are scaled:

```c
mtx.m[0][0] = STATIC_WORLD_SCALE;
mtx.m[1][1] = STATIC_WORLD_SCALE;
mtx.m[2][2] = STATIC_WORLD_SCALE;
```

The distant renderer therefore operates in a different numerical representation from the detailed renderer.

---

# 7. Why Z-Buffering Can Be Disabled

Once distant geometry is compressed and reduced to a set of coarse terrain representations, the renderer does not need hardware depth testing for every triangle.

Instead:

```text
distance
   │
   ▼
sort geometry
   │
   ▼
farthest → nearest
   │
   ▼
render without Z-buffer
```

The critical requirement is that surfaces must be drawn in an order that produces the desired visibility.

This converts the problem from:

```text
hardware depth comparison
```

into:

```text
CPU-side / engine-side ordering
```

That is considerably cheaper than attempting to maintain precise depth relationships over the entire world.

---

# 8. Distant Geometry Ordering

The renderer builds an array:

```c
struct overworld_lod1_sort_entry {
    uint32_t priority;
    struct tmesh* mesh;
};
```

Each visible LOD entry receives a distance-derived priority:

```c
entry->priority =
    (distance >> 2)
    + ((uint32_t)curr->priority << 24);
```

The entries are then sorted:

```c
qsort(
    order,
    final_count,
    sizeof(struct overworld_lod1_sort_entry),
    overworld_entry_sort
);
```

The comparison function sorts the entries by priority.

Therefore the distant renderer can intentionally control draw order rather than relying on the Z-buffer.

---

# 9. LOD Hierarchy

The world should not be represented as one giant mesh.

Instead:

```text
World
 ├── Region
 │    ├── LOD 0
 │    ├── LOD 1
 │    └── LOD 2
 │
 ├── Region
 │    ├── LOD 0
 │    ├── LOD 1
 │    └── LOD 2
 │
 └── ...
```

The implementation stores LOD entries in a hierarchy using:

```c
curr->child_count
curr->lod_scale
curr->priority
curr->meshes[]
```

This allows a parent representation to replace its children when they are sufficiently far away.

---

# 10. LOD Selection

For every candidate terrain element:

```text
distance² =
    dx² + dz²
```

The renderer determines whether a higher-detail child representation is necessary.

The implementation uses:

```c
#define LEVEL2_MIN_DISTANCE 500
```

and evaluates:

```c
distance <
LEVEL2_MIN_DISTANCE²
× curr->lod_scale²
```

When the camera is sufficiently close, the renderer retains the children.

When the camera is sufficiently far away, the parent representation replaces them.

Conceptually:

```text
Camera
  │
  ├── very close
  │     └── LOD 0
  │
  ├── medium
  │     └── LOD 1
  │
  └── far
        └── LOD 2 / baked representation
```

---

# 11. Directional LOD

An important optimization is that the renderer does not necessarily use the same mesh representation for a terrain tile from every direction.

Each LOD entry contains:

```c
curr->meshes[tile_dir]
```

The direction is selected from the relative camera position.

The renderer reduces the world into four primary horizontal directions:

```text
             NORTH
               ↑
               │
WEST ◄─────────┼─────────► EAST
               │
               ↓
             SOUTH
```

The function:

```c
overworld_lod_1_direction_index()
```

chooses one of four representations.

This can be used to provide:

* optimized silhouettes
* different baked views
* better terrain coverage
* reduced geometry
* view-dependent representations

---

# 12. Camera-Direction Optimization

When the player is very close to a tile, using the camera's actual facing direction is preferable to simply using the direction from tile to camera.

The implementation switches direction when:

```c
abs(delta.x) < 200 &&
abs(delta.y) < 200
```

and uses:

```c
tile_dir = camera_dir;
```

This prevents unstable directional selection around the center of the world.

---

# 13. Visibility Culling

The distant world cannot simply render every tile.

A large percentage of the world will be outside the camera frustum.

The renderer therefore creates two horizontal clipping planes.

```c
vector2s16_t clipping_planes[2];
```

They are generated using the camera orientation and FOV:

```c
overworld_create_2d_clipping_planes(...)
```

Each candidate tile is tested against these planes.

Conceptually:

```text
               CAMERA
                 │
            ┌────┴────┐
           /           \
          /   VISIBLE   \
         /               \
        /                 \
───────/───────────────────\──────
       clipping planes
```

Tiles outside the visible region are skipped before rendering.

---

# 14. Top-View Visibility Polygon

For detailed terrain, visibility is calculated differently.

The renderer obtains the inverse view-projection matrix:

```c
matrixInv(view_proj_matrix, view_inv)
```

It transforms the eight corners of the camera frustum back into world space.

Those points are projected onto the X/Z plane.

This creates a 2D polygon representing the camera's visible footprint over the terrain.

Conceptually:

```text
3D frustum
     │
     ▼
inverse VP
     │
     ▼
world-space corners
     │
     ▼
X/Z projection
     │
     ▼
2D visibility polygon
```

The resulting polygon is stored in:

```c
state.loop[]
```

---

# 15. Scanline Tile Enumeration

Instead of testing every tile in the entire world against the polygon, the implementation effectively walks the polygon row by row.

The function:

```c
overworld_step()
```

produces:

```c
struct overworld_tile_slice {
    int min_x;
    int max_x;
    int y;
    bool has_more;
};
```

So visibility becomes:

```text
row Y=10: tiles 15 → 22
row Y=11: tiles 14 → 23
row Y=12: tiles 13 → 24
row Y=13: tiles 12 → 25
...
```

This is extremely useful on the N64 because it avoids expensive per-tile polygon tests.

---

# 16. Terrain Tiling

The world is divided into terrain blocks.

Conceptually:

```text
+----+----+----+----+----+
|    |    |    |    |    |
+----+----+----+----+----+
|    |    |CAM |    |    |
+----+----+----+----+----+
|    |    |    |    |    |
+----+----+----+----+----+
```

Each block can contain multiple vertical layers.

The renderer calculates the relevant Y range:

```c
min_y =
floor(
    (camera_y - block_start_y - camera_far)
    * inv_tile_size
);

max_y =
ceil(
    (camera_y - block_start_y + camera_far)
    * inv_tile_size
);
```

Only layers intersecting the camera's vertical range are enumerated.

---

# 17. Streaming

Terrain blocks are not assumed to remain permanently resident.

Each render block contains:

```text
block
 ├── x
 ├── z
 ├── layers
 ├── starting_y
 └── tile
```

If the requested block is not currently loaded:

```c
if (!block->layers ||
    block->x != x ||
    block->z != z)
{
    overworld->load_next.x = x;
    overworld->load_next.y = z;
    return curr;
}
```

The renderer records the required block for subsequent loading.

This enables a streaming architecture:

```text
VISIBLE
   │
   ▼
required tile
   │
   ├── resident ──► render
   │
   └── missing ───► request load
                       │
                       ▼
                  asynchronous/
                  incremental load
```

---

# 18. Near-Field Rendering

Once the distant representation has been drawn, the renderer switches back to the normal viewport.

The detailed tiles are rendered using the actual world coordinate system.

Each tile receives a transformation matrix relative to the camera:

```c
t3d_mat4_translate(
    &mtx,
    (x * tile_size + min.x - camera_x) * WORLD_SCALE,
    (y * tile_size + starting_y - camera_y) * WORLD_SCALE,
    (z * tile_size + min.y - camera_z) * WORLD_SCALE
);
```

This keeps local coordinates numerically small.

---

# 19. Local Coordinate Origin

The near-world renderer effectively uses:

```text
camera
  │
  └── world origin shifted to camera
```

Instead of rendering:

```text
world position = 250000, 0, 300000
```

it renders:

```text
relative position = 100, 0, -50
```

This improves numerical stability for local geometry.

This principle should be retained even if the world is much larger than the visible area.

---

# 20. Near-Field Render Passes

The detailed renderer has additional subpasses.

### Pass 1 — Low-priority geometry

```c
overworld_render_low_priority()
```

This renders scrolling/pre-world geometry with Z disabled:

```c
rdpq_mode_zbuf(false, false);
```

Examples may include:

* water/background surfaces
* scrolling environmental layers
* special non-depth-tested geometry

---

### Pass 2 — Main tile geometry

The renderer synchronizes and enables Z:

```c
rdpq_sync_pipe();
rdpq_mode_zbuf(true, true);
```

Then:

```c
overworld_render_tile()
```

renders the primary tile render block.

This is the normal detailed world.

---

### Pass 3 — Particles

Finally:

```c
overworld_render_particles()
```

renders static particle instances associated with the terrain block.

---

# 21. Complete Frame Order

The intended high-level frame sequence is:

```text
FRAME
 │
 ├── Camera update
 │
 ├── Calculate visible world
 │
 ├── Distant LOD
 │      │
 │      ├── compressed coordinates
 │      ├── frustum culling
 │      ├── LOD selection
 │      ├── directional mesh selection
 │      ├── distance sorting
 │      ├── Z OFF
 │      └── render
 │
 ├── Restore normal viewport
 │
 ├── Enumerate near tiles
 │
 ├── Low-priority tile pass
 │      └── Z OFF
 │
 ├── Main terrain pass
 │      └── Z ON
 │
 ├── Particle pass
 │
 └── Present
```

The actual renderer follows this architecture: LOD1 is rendered first, then visible tiles are enumerated, low-priority geometry is rendered, Z-buffering is enabled, main tiles are rendered, and particles are processed.

---

# 22. RSPQ Render Blocks

Geometry should not require expensive CPU-side command construction every frame.

Instead, geometry/material combinations should be precompiled into RSPQ blocks.

Conceptually:

```text
Asset
 │
 ▼
Blender
 │
 ▼
Mesh exporter
 │
 ▼
N64 mesh/material representation
 │
 ▼
RSPQ block
 │
 ▼
frame rendering
```

The runtime can then execute:

```c
rspq_block_run(mesh->block);
```

This significantly reduces CPU overhead.

---

# 23. Material Batching

Each mesh references a material:

```c
mesh->material
```

The renderer tracks the currently active material:

```c
struct material* mat = NULL;
```

Before rendering:

```c
if (mat != mesh->material) {
    material_apply(mesh->material);
    mat = mesh->material;
}
```

Therefore materials are changed only when necessary.

This is particularly important on the N64 because excessive RDP state changes are expensive.

---

# 24. Why Baked Distant Terrain Is Important

The distant renderer is fundamentally different from the near renderer.

The near renderer can afford:

```text
mesh
 ├── material A
 ├── material B
 ├── material C
 ├── lighting
 └── geometry
```

The distant renderer wants:

```text
terrain chunk
     │
     ▼
single optimized representation
```

The reason is that distant geometry is primarily serving as a visual backdrop.

It does not need the full material complexity of the original geometry.

Therefore the asset pipeline should support a baking stage:

```text
high-detail geometry
        +
textures
        +
lighting
        +
AO
        +
terrain color
        │
        ▼
   baked texture
        │
        ▼
 low-detail mesh
```

This dramatically reduces runtime rendering complexity.

---

# 25. Recommended Baking Pipeline

For each terrain chunk:

```text
Blender scene
      │
      ├── geometry
      ├── textures
      ├── lights
      └── materials
             │
             ▼
       offline bake
             │
       ┌─────┴─────┐
       ▼           ▼
  low-poly mesh   baked texture
       │           │
       └─────┬─────┘
             ▼
       N64 asset
```

The distant asset should contain enough visual information to look convincing at its intended distance without requiring the original material graph.

---

# 26. Fog / Atmospheric Fading

Distance alone is not sufficient.

The renderer needs an atmospheric transition between:

```text
near geometry
      │
      ▼
medium distance
      │
      ▼
fog
      │
      ▼
sky/background
```

The material system supports explicit fog commands.

The material format includes:

```c
COMMAND_FOG
COMMAND_FOG_COLOR
COMMAND_FOG_RANGE
```

and configures the RDP fog mode through:

```c
rdpq_mode_fog(...)
```

with fog color and range stored in the material definition.

---

# 27. Material Fog Model

The asset/material pipeline should expose:

```text
fog.enabled
fog.color
fog.min_distance
fog.max_distance
fog.mode
```

For example:

```json
{
    "fog": {
        "enabled": true,
        "color": [120, 150, 180],
        "min_distance": 300,
        "max_distance": 1200
    }
}
```

The actual implementation clamps the minimum fog range because excessively high values can cause problems:

```c
if (min > BIGGEST_MIN_VALUE) {
    min = BIGGEST_MIN_VALUE;
}
```

---

# 28. Locally Colored Atmosphere

A particularly useful extension is to avoid a single global fog color.

Instead:

```text
mountain region
      ↓
blue/gray atmosphere

forest region
      ↓
green atmosphere

desert region
      ↓
yellow/orange atmosphere
```

This makes the distant world feel much larger because the transition into the horizon is visually integrated with the environment.

The color combiner/material system is the appropriate place to implement this.

---

# 29. Skybox

The distant pass supports a separate skybox transform.

The normal distant transform contains camera-relative translation.

For the skybox transform:

```c
mtx.m[3][0] = 0.0f;
mtx.m[3][1] = 0.0f;
mtx.m[3][2] = 0.0f;
```

The result is:

```text
world geometry
    ↓
camera-relative transform

skybox
    ↓
rotation-only transform
```

Therefore the sky remains stationary relative to the camera while terrain moves.

---

# 30. Memory Architecture

A practical implementation should divide memory into:

```text
ROM
 │
 ├── compressed terrain assets
 ├── baked textures
 ├── mesh data
 └── material definitions
       │
       ▼
RAM / cartridge streaming
       │
       ├── active terrain blocks
       ├── visible mesh metadata
       └── texture cache
              │
              ▼
          frame memory
              │
              ├── matrices
              ├── visible tile list
              ├── temporary sorting arrays
              └── particle data
```

The existing implementation uses a frame memory pool for transient allocations such as matrices and visible tile information.

---

# 31. Visible Tile Budget

The renderer should have a hard upper bound on visible tile metadata.

The current implementation uses:

```c
overworld_tile_render_info_t tiles[8];
```

This is an example of an important N64 design philosophy:

> Never allow visibility to create unbounded per-frame work.

The engine should define explicit budgets for:

```text
maximum visible terrain chunks
maximum LOD entries
maximum triangles
maximum material changes
maximum particles
maximum texture uploads
maximum streaming operations
```

---

# 32. Proposed Engine Data Structures

A reimplementation should use something similar to:

```c
typedef struct {
    int x;
    int z;

    float world_size;

    int lod_level;

    Mesh* lod_meshes[4];

    uint16_t child_count;

    float lod_scale;

    Material* material;
} WorldLODEntry;
```

Terrain:

```c
typedef struct {
    int x;
    int z;

    int layer_count;

    float start_y;

    TerrainLayer* layers;

    ParticleSet* particles;
} TerrainBlock;
```

Terrain layer:

```c
typedef struct {
    RSPQBlock* render_block;

    Mesh* scrolling_meshes;

    int scrolling_mesh_count;
    int pre_scrolling_mesh_count;
} TerrainLayer;
```

---

# 33. Visibility Algorithm

## Input

```text
camera
world
view-projection matrix
```

## Output

```text
visible distant LOD entries
visible near terrain tiles
```

### Step 1

Calculate camera position and orientation.

### Step 2

Create distant-world camera.

### Step 3

Create distant clipping planes.

### Step 4

Iterate LOD hierarchy.

### Step 5

Cull entries outside clipping planes.

### Step 6

Calculate distance.

### Step 7

Select LOD level.

### Step 8

Select directional mesh.

### Step 9

Add entry to render list.

### Step 10

Sort distant entries.

### Step 11

Render distant world with Z disabled.

### Step 12

Calculate detailed-world frustum polygon.

### Step 13

Scan polygon to enumerate visible tile ranges.

### Step 14

Load/request missing terrain blocks.

### Step 15

Render low-priority geometry.

### Step 16

Enable Z-buffer.

### Step 17

Render detailed terrain.

### Step 18

Render particles.

---

# 34. Pseudocode

```c
void render_world(World* world, Camera* camera)
{
    // ------------------------------------------------
    // 1. Determine camera visibility
    // ------------------------------------------------

    Frustum frustum = camera_get_frustum(camera);

    // ------------------------------------------------
    // 2. Distant world
    // ------------------------------------------------

    Camera lod_camera = create_lod_camera(camera);

    LODRenderList list;

    collect_lod_entries(
        world->lod_root,
        camera,
        &frustum,
        &list
    );

    sort_back_to_front(&list);

    renderer_set_zbuffer(false);

    renderer_set_world_scale(LOD_WORLD_SCALE);

    render_lod_entries(&list);

    renderer_set_zbuffer(true);

    // ------------------------------------------------
    // 3. Near world visibility
    // ------------------------------------------------

    Polygon visible_region =
        calculate_ground_visibility_polygon(camera);

    TileList tiles;

    enumerate_visible_tiles(
        world,
        &visible_region,
        &tiles
    );

    // ------------------------------------------------
    // 4. Streaming
    // ------------------------------------------------

    for each tile in tiles:
        if (!tile.resident)
            request_tile(tile);

    // ------------------------------------------------
    // 5. Non-depth-tested geometry
    // ------------------------------------------------

    renderer_set_zbuffer(false);

    for each tile in tiles:
        render_low_priority(tile);

    // ------------------------------------------------
    // 6. Detailed geometry
    // ------------------------------------------------

    renderer_set_zbuffer(true);

    for each tile in tiles:
        render_detailed_tile(tile);

    // ------------------------------------------------
    // 7. Particles
    // ------------------------------------------------

    for each tile in tiles:
        render_particles(tile);
}
```

---

# 35. Asset Pipeline

The asset pipeline is just as important as the runtime renderer.

Recommended pipeline:

```text
                 BLENDER
                    │
       ┌────────────┼────────────┐
       │            │            │
       ▼            ▼            ▼
    geometry     materials     lighting
       │            │            │
       └──────┬─────┴────────────┘
              │
              ▼
        asset processing
              │
       ┌──────┴──────────┐
       │                 │
       ▼                 ▼
  near-world         distant-world
    assets             assets
       │                 │
       ▼                 ▼
 high detail          baked
 normal materials     low-poly
       │                 │
       └────────┬────────┘
                ▼
              ROM
```

The repository's build system exports game files and levels from Blender files, and its documented build uses libdragon, Tiny3D and Blender.

---

# 36. Near vs Distant Asset Requirements

| Property         |           Near |         Distant |
| ---------------- | -------------: | --------------: |
| Geometry         |           High |             Low |
| Z-buffer         |            Yes |              No |
| Materials        |           Full |      Simplified |
| Textures         |       Detailed |           Baked |
| Lighting         | Runtime/vertex |           Baked |
| LOD              |        Highest |           Lower |
| Coordinate scale |         Normal |      Compressed |
| Sorting          |     Hardware Z |        Explicit |
| Streaming        |       Required | More aggressive |
| Fog              |       Optional |       Important |
| Particles        |            Yes |      Usually no |

---

# 37. What Makes the World Appear Seamless

The player must not perceive the boundary between the two rendering systems.

The transition should be:

```text
             DISTANT
                │
                │
        baked low-detail
                │
             transition
                │
        medium-detail LOD
                │
             transition
                │
          high-detail
                │
              PLAYER
```

The transition is hidden using:

1. LOD selection
2. atmospheric fog
3. similar lighting
4. baked textures
5. overlapping geometry
6. camera-relative coordinates
7. directional representations
8. carefully chosen LOD distances

The goal is not mathematical continuity.

The goal is:

> perceptual continuity.

---

# 38. Why This Works

The technique effectively decomposes the open-world rendering problem into three different problems.

### Problem 1 — Extremely far geometry

Solution:

```text
compress coordinates
+
remove Z-buffer
+
sort manually
+
reduce geometry
```

### Problem 2 — Medium-distance terrain

Solution:

```text
LOD hierarchy
+
baked representations
+
fog
```

### Problem 3 — Gameplay-scale geometry

Solution:

```text
camera-relative coordinates
+
Z-buffer
+
high-detail meshes
```

Instead of forcing the N64 to solve:

```text
ONE HUGE WORLD
```

we give it:

```text
SMALL LOCAL WORLD
+
COMPRESSED DISTANT WORLD
```

---

# 39. Important Design Principle

The biggest lesson from the implementation is:

> The far clipping plane does not have to represent the same coordinate system as the gameplay world.

This is the conceptual breakthrough.

A conventional engine assumes:

```text
world coordinates
      ↓
camera
      ↓
projection
      ↓
Z-buffer
```

This architecture instead does:

```text
                 WORLD
                   │
          ┌────────┴────────┐
          │                 │
          ▼                 ▼
     FAR REPRESENTATION   LOCAL REPRESENTATION
          │                 │
    compressed scale       normal scale
          │                 │
      Z disabled           Z enabled
          │                 │
    manual ordering        hardware depth
          │                 │
          └────────┬────────┘
                   ▼
                 IMAGE
```

That is why it can visually represent a world far larger than the N64's normal depth-buffer precision would suggest.

---

# 40. Implementation Requirements

A reimplementation is considered successful when it satisfies:

### Rendering

* [ ] Distant world renders with Z-buffer disabled.
* [ ] Near world renders with Z-buffer enabled.
* [ ] Distant world uses a separate coordinate scale.
* [ ] Distant geometry is explicitly sorted.
* [ ] Near geometry uses hardware depth testing.

### LOD

* [ ] Terrain is divided into chunks.
* [ ] LOD hierarchy exists.
* [ ] LOD selection is distance based.
* [ ] Directional LOD variants are supported.
* [ ] Child geometry is skipped when parent LOD is sufficient.

### Visibility

* [ ] Distant frustum culling exists.
* [ ] Near-world visibility is calculated independently.
* [ ] Invisible tiles are never submitted to the renderer.

### Streaming

* [ ] Terrain blocks can be loaded independently.
* [ ] Missing blocks create streaming requests.
* [ ] Rendering never assumes the entire world is resident.

### Assets

* [ ] Near-world geometry can use detailed materials.
* [ ] Distant geometry supports baked representations.
* [ ] RSPQ blocks are prebuilt where practical.

### Atmosphere

* [ ] Fog supports configurable color.
* [ ] Fog range is configurable.
* [ ] Distant geometry can blend into the atmosphere.

### Performance

* [ ] Visible tile count is bounded.
* [ ] Material state changes are minimized.
* [ ] Temporary frame allocations use a frame allocator.
* [ ] Rendering work is represented by bounded lists.

---

# 41. Recommended Development Order

Do NOT attempt to build the complete system simultaneously.

Implement it in this order:

```text
Phase 1
───────
Basic N64 camera
       │
       ▼
Single terrain tile
       │
       ▼
Multiple terrain tiles
```

```text
Phase 2
───────
Tile visibility
       │
       ▼
Frustum culling
       │
       ▼
Streaming
```

```text
Phase 3
───────
LOD hierarchy
       │
       ▼
LOD selection
       │
       ▼
Directional LOD
```

```text
Phase 4
───────
Distant coordinate compression
       │
       ▼
Z-buffer OFF
       │
       ▼
Back-to-front sorting
```

```text
Phase 5
───────
Baked distant terrain
       │
       ▼
Fog
       │
       ▼
LOD transition tuning
```

```text
Phase 6
───────
Particles
       │
       ▼
Water
       │
       ▼
Dynamic objects
       │
       ▼
Final optimization
```

---

# 42. Minimal Proof-of-Concept

Before building a complete open world, create a test scene containing:

```text
             mountain
                ▲
               / \
              /   \
       tree   /     \   tower
        │    /       \    │
        │   /         \   │
────────┴──/───────────\──┴────────
             terrain
                 ▲
                 │
               player
```

The test should contain:

* 4×4 terrain chunks
* 3 LOD levels
* 1 baked distant representation
* 1 detailed near representation
* fog
* camera movement
* Z-buffer switching
* tile streaming simulation

Then test:

```text
walk forward
walk backward
rotate 360°
move diagonally
approach LOD boundaries
cross tile boundaries
look toward horizon
```

The critical test is whether the player can move continuously without seeing:

* Z-fighting
* popping
* holes
* visible tile loading
* coordinate precision artifacts
* obvious LOD boundaries

---

# 43. Debug Visualization

The engine should expose debug modes.

Recommended:

```text
F1 = normal
F2 = show tile boundaries
F3 = show LOD level
F4 = show distant pass only
F5 = show near pass only
F6 = show visibility polygon
F7 = show streaming state
F8 = show Z-buffer state
F9 = show draw order
```

The original renderer already contains LOD render debugging functionality such as:

```c
ENABLE_LOD_RENDER_DEBUG
LOD_RENDER_MODE_DETAILED
LOD_RENDER_MODE_LOD3
```

which can be used as a model for the debugging architecture.

---

# 44. Performance Instrumentation

Every major stage should be profiled independently:

```text
LOD collection
LOD sorting
LOD rendering

visibility polygon
tile enumeration
tile loading

low-priority rendering
main tile rendering
particle rendering
```

The existing implementation uses profiling scopes around these stages, making it possible to identify which part of the renderer is consuming frame time.

---

# 45. Final Architecture

The complete system should ultimately look like:

```text
                         GAME WORLD
                             │
                             ▼
                     ┌───────────────┐
                     │ World Manager │
                     └───────┬───────┘
                             │
                ┌────────────┴────────────┐
                │                         │
                ▼                         ▼
         Distant LOD System          Near Tile System
                │                         │
        ┌───────┼────────┐          ┌─────┼─────┐
        │       │        │          │     │     │
      CULL    LOD     SORT       CULL  LOAD  LAYERS
        │       │        │          │     │     │
        └───────┼────────┘          └─────┼─────┘
                │                         │
                ▼                         ▼
          Z-BUFFER OFF              Z-BUFFER ON
                │                         │
                ▼                         ▼
        compressed world            local world
                │                         │
                └──────────┬──────────────┘
                           ▼
                     FINAL FRAME
```

The result is not technically an infinite world.

It is a carefully constructed illusion where:

```text
              FAR
               │
      ┌────────┴────────┐
      │ compressed      │
      │ representation  │
      └────────┬────────┘
               │
            LOD fade
               │
      ┌────────┴────────┐
      │ detailed local  │
      │ representation  │
      └────────┬────────┘
               │
             PLAYER
```

The N64 never needs to depth-test the entire world simultaneously.

That is the fundamental technique that makes the apparent open world possible.
