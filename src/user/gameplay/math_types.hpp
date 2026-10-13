#pragma once

namespace madeline_cube {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// OG is Z-up; demake uses +Y up and maps OG +Y travel to world -Z.
constexpr Vec3 FromOgAxes(const Vec3& og) { return {og.x, og.z, -og.y}; }
constexpr Vec3 ToOgAxes(const Vec3& world) { return {world.x, -world.z, world.y}; }
// Runtime player positions are body centers; OG Player.Position is feet.
constexpr Vec3 BodyFeet(const Vec3& center, float half_height) {
    return {center.x, center.y - half_height, center.z};
}
constexpr Vec3 BodyCenter(const Vec3& feet, float half_height) {
    return {feet.x, feet.y + half_height, feet.z};
}
constexpr Vec3 ProbeAboveFeet(const Vec3& center, float half_height, float offset) {
    return {center.x, center.y - half_height + offset, center.z};
}

}  // namespace madeline_cube

