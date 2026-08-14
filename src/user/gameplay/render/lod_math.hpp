#pragma once

#include <algorithm>
#include <cmath>

#include "gameplay/math_types.hpp"
#include "gameplay/world/mappack_loader.hpp"  // AABB, V2RoomSpec

namespace madeline_cube {

// LOD threshold constant (arch.md §10). The squared-distance threshold below
// which a higher-detail child representation is retained.
inline constexpr float kLevel2MinDistance = 500.0f;

// Pi for degree->radian conversions (no global constant in math_types.hpp).
inline constexpr float kLodPi = 3.14159265358979f;

// Squared-distance threshold beyond which a distant cell is skipped (Inc 2 /
// D1). Initial value = (distant_far * 0.4)^2 so the drop coincides with fog
// onset; tuned in Inc 5. MUST be non-zero or the falloff is a no-op (the
// `max_dist2 <= 0` rule below treats 0 as "no limit").
// For the Forsaken City map: distant_far ≈ 3323, fog onset ≈ 1329, so
// (1329)^2 ≈ 1.77e6. Cells beyond ~1329 world units fade into fog.
//
// Fog range (Inc 6): the runtime fog is `sqrt(kDistantMaxDist2) * 0.4 → 0.9`
// = ~532 → 1197, so fog COMPLETES before the ~1330 drop (no pop at the drop
// edge). The invariant (fog completes before the drop) holds.
//
// Inc 2 / D2 (distant-pass perf): this constant is MAP-SPECIFIC (bakes in the
// Forsaken City diagonal; re-derive per map). It is the single source of truth
// for the distant-pass drop threshold AND the fog range — `gameplay_scene.cpp`
// derives the fog from `sqrt(kDistantMaxDist2)` so the drop/fog coupling lives
// in one place. INVARIANT: the fog range must COMPLETE before
// `sqrt(kDistantMaxDist2)` so cells dropped by the falloff are already fully
// fogged (no pop at the drop edge). If this constant changes, the fog follows
// automatically; keep the fog max ratio ≤ 1.0.
inline constexpr float kDistantMaxDist2 = 1.77e6f;

// Returns true if `origin` is within `max_dist2` of `cam_pos` (XZ plane).
// `max_dist2 <= 0` → treat as "no distance limit" (all cells pass) so a bad
// constant never blanks the horizon. Host-safe — pure Vec3/float.
inline bool CellWithinDistance(const Vec3& cam_pos, const Vec3& origin,
                               float max_dist2) {
    if (max_dist2 <= 0.0f) return true;  // no limit
    const float dx = origin.x - cam_pos.x;
    const float dz = origin.z - cam_pos.z;
    return (dx * dx + dz * dz) <= max_dist2;
}

// Chebyshev distance between two grid cells (Inc 6 / D5). Host-safe.
inline int ChebyshevCellDistance(int ix0, int iz0, int ix1, int iz1) {
    const int dx = ix0 - ix1 < 0 ? ix1 - ix0 : ix0 - ix1;
    const int dz = iz0 - iz1 < 0 ? iz1 - iz0 : iz0 - iz1;
    return dx > dz ? dx : dz;
}

// The D5 stream-radius invariant (Inc 6 / D5): the worst-case load distance
// (radius × cell − half-cell, because distance tests hit the cell center) must
// stay ≥ the fog-complete distance so a cell is always fully fogged before it
// can become drawable (eviction/load is invisible). Returns the minimum radius
// that satisfies the invariant for the given cell size + fog-complete distance.
// Host-safe.
inline int MinStreamRadiusForFog(float cell_size, float fog_complete_dist) {
    // radius × cell − cell/2 ≥ fog_complete  ⇒  radius ≥ (fog_complete + cell/2)/cell
    const float r = (fog_complete_dist + cell_size * 0.5f) / cell_size;
    return static_cast<int>(std::ceil(r));
}

// Build the shared camera-relative matrix for the whole distant pass
// (Inc 3 / D2). All distant verts are packed relative to `shared_origin`
// (the map center) at `kLodScale`; the pass draws every cell's block under
// ONE matrix that maps packed int16 back to camera-relative world space:
//   drawn = kInvScale * (world - shared_origin) * kLodScale + (shared_origin - cam)
//         = (world - shared_origin) + (shared_origin - cam)
//         = world - cam
// Returns the 4×4 matrix as 16 floats (row-major, matching T3DMat4 layout:
// scale on the diagonal, translation in the last column). Host-safe — pure
// float math, no N64 types. `lod_scale` is the packed coordinate scale
// (kLodScale = 0.25); `inv_scale` = 1/lod_scale.
inline void BuildSharedPassMatrix(const Vec3& cam_pos, const Vec3& shared_origin,
                                  float lod_scale, float out[16]) {
    const float inv = 1.0f / lod_scale;
    // Row-major 4×4: scale on the diagonal, translation in the last column.
    for (int i = 0; i < 16; ++i) out[i] = 0.0f;
    out[0] = inv; out[5] = inv; out[10] = inv; out[15] = 1.0f;
    out[12] = shared_origin.x - cam_pos.x;
    out[13] = shared_origin.y - cam_pos.y;
    out[14] = shared_origin.z - cam_pos.z;
}

// Worst-case camera→cell distance = full map diagonal × margin. Used as the
// distant pass far clip (cull + projection). Covers a camera at any map corner
// seeing the opposite corner. Returns 0 if `bounds` is null (caller falls back
// to a default far).
inline float MapFarClipDistance(const AABB* bounds, float margin = 1.15f) {
    if (!bounds) return 0.0f;
    const float cx = (bounds->min.x + bounds->max.x) * 0.5f;
    const float cz = (bounds->min.z + bounds->max.z) * 0.5f;
    const float dx = std::max(std::fabs(bounds->min.x - cx),
                              std::fabs(bounds->max.x - cx));
    const float dz = std::max(std::fabs(bounds->min.z - cz),
                              std::fabs(bounds->max.z - cz));
    return 2.0f * std::sqrt(dx * dx + dz * dz) * margin;  // diameter × margin
}

// Union of room AABBs (world XZ extent). Returns an empty AABB if count==0.
inline AABB UnionRoomsAABB(const V2RoomSpec* rooms, int count) {
    AABB u = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    if (!rooms || count <= 0) return u;
    u = rooms[0].world_aabb;
    for (int i = 1; i < count; ++i) {
        u.min.x = std::min(u.min.x, rooms[i].world_aabb.min.x);
        u.min.z = std::min(u.min.z, rooms[i].world_aabb.min.z);
        u.max.x = std::max(u.max.x, rooms[i].world_aabb.max.x);
        u.max.z = std::max(u.max.z, rooms[i].world_aabb.max.z);
    }
    return u;
}

// Returns true if `origin` (a cell center in world XZ) is inside the camera's
// horizontal view cone AND depth range [near_d, far_d]. `hfov_deg` is the
// horizontal FULL field of view in degrees (e.g. computed from the vertical
// FOV + 4:3 aspect: hfov = 2·atan(tan(vfov/2)·(4/3))). `margin` widens the
// cone (>1 = wider) so horizon cells don't pop at the exact screen edge.
// A degenerate facing (camera at target), empty depth range (near >= far), or
// cell at the camera's own position are treated as not-visible (safe).
inline bool CellInDistantFrustum(const Vec3& cam_pos, const Vec3& cam_target,
                                 float hfov_deg, float near_d, float far_d,
                                 const Vec3& origin, float margin = 1.15f) {
    if (near_d >= far_d) return false;          // empty frustum
    if (far_d <= 0.0f) return false;
    if (margin < 1.0f) margin = 1.0f;

    // Horizontal facing direction (camera -> target, XZ plane).
    const float fx = cam_target.x - cam_pos.x;
    const float fz = cam_target.z - cam_pos.z;
    const float flen2 = fx * fx + fz * fz;
    if (flen2 < 1e-6f) return false;            // degenerate facing — cull
    const float inv_flen = 1.0f / std::sqrt(flen2);
    const float fdx = fx * inv_flen;
    const float fdz = fz * inv_flen;

    // Delta from camera to the cell center (XZ plane).
    const float dx = origin.x - cam_pos.x;
    const float dz = origin.z - cam_pos.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    if (dist < near_d || dist > far_d) return false;  // depth range

    // Angular test: the cell is inside the cone when the angle between the
    // facing and the cell direction is within the (margined) half-FOV.
    const float inv_dist = dist > 1e-6f ? 1.0f / dist : 0.0f;
    const float dot = (dx * fdx + dz * fdz) * inv_dist;
    const float half_rad = (hfov_deg * 0.5f) * (kLodPi / 180.0f) * margin;
    const float cos_half = std::cos(half_rad);
    return dot >= cos_half;
}

// Frustum-cull cone margin (Inc 4 / D3). >1 widens the cone so horizon cells
// don't pop at the exact screen edge. Shared by the renderer and the
// host-safe AABB cull helper below (single source of truth). Tuned on device.
inline constexpr float kCullMargin = 1.15f;

// Cull a distant cell only when its WHOLE XZ AABB is outside the camera cone
// + depth range (Inc 4 / D3). Per-corner angular test widened by
// `atan(half_diag / dist)` so the cone cannot pass through the cell interior
// with all 4 corners outside (the exact screen-edge pop-in bug class); near/far
// depth slack scaled by the cell half-extent. Keeps the cell if ANY corner
// passes. Host-safe — pure Vec3/AABB/float math.
inline bool CellAabbInDistantFrustum(const Vec3& cam_pos, const Vec3& cam_target,
                                     float hfov_deg, float near_d, float far_d,
                                     const AABB& aabb,
                                     float margin = kCullMargin,
                                     float extent_slack = 1.0f) {
    if (near_d >= far_d) return false;          // empty frustum
    if (far_d <= 0.0f) return false;
    if (margin < 1.0f) margin = 1.0f;

    // Horizontal facing direction (camera -> target, XZ plane).
    const float fx = cam_target.x - cam_pos.x;
    const float fz = cam_target.z - cam_pos.z;
    const float flen2 = fx * fx + fz * fz;
    if (flen2 < 1e-6f) return false;            // degenerate facing — cull
    const float inv_flen = 1.0f / std::sqrt(flen2);
    const float fdx = fx * inv_flen;
    const float fdz = fz * inv_flen;

    // Cell XZ half-extent + half-diagonal (≈170u for a 240u cell).
    const float hx = (aabb.max.x - aabb.min.x) * 0.5f;
    const float hz = (aabb.max.z - aabb.min.z) * 0.5f;
    const float half_diag = std::sqrt(hx * hx + hz * hz);

    const float half_rad = (hfov_deg * 0.5f) * (kLodPi / 180.0f) * margin;

    // Test each of the 4 XZ corners. A corner passes if it is within the
    // (margined + extent-widened) cone AND within the depth range (with
    // extent slack). Keep the cell if ANY corner passes.
    const float corners[4][2] = {
        {aabb.min.x, aabb.min.z}, {aabb.max.x, aabb.min.z},
        {aabb.min.x, aabb.max.z}, {aabb.max.x, aabb.max.z},
    };
    for (int c = 0; c < 4; ++c) {
        const float dx = corners[c][0] - cam_pos.x;
        const float dz = corners[c][1] - cam_pos.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        // Depth range with extent slack.
        if (dist < near_d - extent_slack * half_diag) continue;
        if (dist > far_d + extent_slack * half_diag) continue;
        // Angular test widened by atan(half_diag / dist) so the cone cannot
        // pass through the cell interior with all 4 corners outside.
        const float inv_dist = dist > 1e-6f ? 1.0f / dist : 0.0f;
        const float dot = (dx * fdx + dz * fdz) * inv_dist;
        const float widen = (dist > 1e-6f) ? std::atan(half_diag / dist) : 0.0f;
        const float cos_widened = std::cos(half_rad + widen);
        if (dot >= cos_widened) return true;
    }
    return false;
}

// A resident cell is drawn by the near pass iff its AABB intersects the camera
// cone (near FOV/depth) — no grid-index cut, no radial extent beyond residency
// (the ring IS the coverage; see D4). Host-safe. Reuses the Inc 4 corner math.
// Depth 20..800 is effectively a no-op for the ring (all ring cells < 800u,
// the camera's own cell always in range) — the cone is the only effective
// cull and is cheap (4 corners × a dot product).
//
// `fov_deg` is the VERTICAL FOV (as passed to t3d_viewport_set_projection);
// the cone is widened to the horizontal FOV (4:3 aspect) so ring cells at
// the screen's left/right edges are kept — matching the distant pass's
// horizontal cone (see DistantWorldRenderer::Render).
inline bool CellAabbInNearCone(const Vec3& cam_pos, const Vec3& cam_target,
                               float fov_deg, float near_d, float far_d,
                               const AABB& aabb) {
    if (near_d >= far_d) return false;
    if (far_d <= 0.0f) return false;

    // Horizontal facing direction (camera -> target, XZ plane).
    const float fx = cam_target.x - cam_pos.x;
    const float fz = cam_target.z - cam_pos.z;
    const float flen2 = fx * fx + fz * fz;
    if (flen2 < 1e-6f) return true;  // degenerate facing — draw (safe)
    const float inv_flen = 1.0f / std::sqrt(flen2);
    const float fdx = fx * inv_flen;
    const float fdz = fz * inv_flen;

    // Horizontal full FOV from the vertical FOV + 4:3 aspect (same conversion
    // as the distant pass), so the near cone matches the visible horizontal
    // frustum.
    const float vfov_rad = fov_deg * 0.5f * (kLodPi / 180.0f);
    const float hfov_deg = 2.0f * std::atan(std::tan(vfov_rad) * (4.0f / 3.0f)) *
                           (180.0f / kLodPi);
    const float half_rad = (hfov_deg * 0.5f) * (kLodPi / 180.0f);
    const float cos_half = std::cos(half_rad);

    // Test each of the 4 XZ corners. Keep the cell if ANY corner is within
    // the cone AND the depth range.
    const float corners[4][2] = {
        {aabb.min.x, aabb.min.z}, {aabb.max.x, aabb.min.z},
        {aabb.min.x, aabb.max.z}, {aabb.max.x, aabb.max.z},
    };
    for (int c = 0; c < 4; ++c) {
        const float dx = corners[c][0] - cam_pos.x;
        const float dz = corners[c][1] - cam_pos.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist < near_d || dist > far_d) continue;
        const float inv_dist = dist > 1e-6f ? 1.0f / dist : 0.0f;
        const float dot = (dx * fdx + dz * fdz) * inv_dist;
        if (dot >= cos_half) return true;
    }
    return false;
}

// Per-direction mesh index (arch.md §11-12). A distant LOD entry can hold up
// to 4 directional meshes (N/S/E/W). This returns the index for a given
// relative direction vector.
//
// The four directions are indexed:
//   0 = +Z (south, toward camera when looking -Z)
//   1 = -Z (north)
//   2 = +X (east)
//   3 = -X (west)
inline int DirectionalIndexFromDelta(const Vec3& delta) {
    const float adx = std::fabs(delta.x);
    const float adz = std::fabs(delta.z);
    if (adx >= adz) {
        return delta.x >= 0.0f ? 2 : 3;  // east / west
    }
    return delta.z >= 0.0f ? 0 : 1;      // south / north
}

// Select the directional mesh index for a tile given the camera position and
// the camera's facing direction (arch.md §12). When the camera is very close
// to the tile center (|delta.x| and |delta.z| both < `close_threshold`), use
// the camera's own direction to avoid unstable directional selection around
// the center of the world; otherwise use the tile->camera delta direction.
inline int DirectionalMeshIndex(const Vec3& camera_pos, const Vec3& tile_origin,
                                const Vec3& camera_dir, float close_threshold) {
    const Vec3 delta = {camera_pos.x - tile_origin.x,
                        camera_pos.y - tile_origin.y,
                        camera_pos.z - tile_origin.z};
    if (std::fabs(delta.x) < close_threshold &&
        std::fabs(delta.z) < close_threshold) {
        return DirectionalIndexFromDelta(camera_dir);
    }
    return DirectionalIndexFromDelta(delta);
}

// Select the LOD level for a distant tile (arch.md §10). Returns the LOD
// level (0 = highest detail, closer) whose squared-distance threshold
// `dist² < kLevel2MinDistance² * lod_scale²` is satisfied. `max_level` bounds
// the returned level (the furthest/baked representation).
//
// The rule: for each level, if the camera-to-tile distance² is LESS than the
// level's threshold, the child (higher-detail) representation is still
// required — so we return that (lower) level. When the distance exceeds all
// thresholds, we return `max_level` (the coarsest).
inline int SelectLodLevel(const Vec3& camera_pos, const Vec3& tile_origin,
                          float lod_scale, int max_level) {
    const float dx = camera_pos.x - tile_origin.x;
    const float dz = camera_pos.z - tile_origin.z;
    const float dist2 = dx * dx + dz * dz;
    const float base = kLevel2MinDistance * kLevel2MinDistance * lod_scale * lod_scale;
    for (int level = 0; level <= max_level; ++level) {
        // Higher levels have larger thresholds (parent replaces children when
        // the distance is large). Scale the base threshold by the level so
        // level 0 is the closest-switch.
        const float threshold = base * (1.0f + static_cast<float>(level));
        if (dist2 < threshold) return level;
    }
    return max_level;
}

}  // namespace madeline_cube
