#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "gameplay/math_types.hpp"

namespace madeline_cube {

// Engine-light port of Celeste64-og/Source/Graphics/Hair.cs. Keep simulation
// in OG's Z-up basis: its plane, side rotation, and floor clamp are all stated
// in that basis. Inputs/outputs cross the existing N64 axis adapter exactly
// once at the boundary.
class PlayerHairSimulation {
public:
    static constexpr int kMaxNodes = 18;

    bool Update(int node_count, float delta_seconds, const Vec3& head_world,
                const Vec3& forward_world, bool grounded, float roundness = 0.0f,
                float forward_offset_per_node = 0.50f) {
        if (node_count < 1 || node_count > kMaxNodes ||
            !std::isfinite(delta_seconds) || delta_seconds < 0.0f ||
            !std::isfinite(roundness) || !std::isfinite(forward_offset_per_node)) {
            return false;
        }

        if (node_count < node_count_) {
            for (int i = node_count; i < node_count_; ++i) nodes_[i] = {};
        } else {
            for (int i = node_count_; i < node_count; ++i) nodes_[i] = {};
        }
        node_count_ = node_count;
        wave_ += delta_seconds * 4.0f;

        const Vec3 head_og = ToOgAxes(head_world);
        const Vec3 forward_og = ToOgAxes(forward_world);
        const Vec3 step = Add(Scale(forward_og, forward_offset_per_node),
                              {0.0f, 0.0f, -(1.0f - roundness)});
        nodes_[0] = head_og;

        Vec3 target = head_og;
        Vec3 previous = head_og;
        const Vec3 side = {forward_og.y, -forward_og.x, 0.0f};
        const Vec3 plane_point = Add(head_og, forward_og);

        for (int i = 1; i < node_count_; ++i) {
            const float p = static_cast<float>(i) / static_cast<float>(node_count_);
            target = Add(target, Scale(side, std::sin(wave_ + i * 0.50f) * 0.50f * p));

            // OG intentionally uses a fixed per-update coefficient; do not
            // make this frame-time-adjusted without source parity evidence.
            nodes_[i] = Add(nodes_[i], Scale(Sub(target, nodes_[i]), 0.25f));

            const float plane_distance = Dot(Sub(nodes_[i], plane_point), forward_og);
            if (plane_distance < 0.0f) {
                nodes_[i] = Sub(nodes_[i], Scale(forward_og, plane_distance));
            }

            if (grounded) {
                const float distance_to_ground = Lerp(4.0f, 6.0f, p);
                if (nodes_[i].z < nodes_[0].z - distance_to_ground) {
                    nodes_[i].z = nodes_[0].z - distance_to_ground;
                }
            }

            const Vec3 parent_delta = Sub(nodes_[i], previous);
            const float parent_distance = Length(parent_delta);
            if (parent_distance > 0.8f) {
                nodes_[i] = Add(previous, Scale(parent_delta, 0.8f / parent_distance));
            }

            target = Add(nodes_[i], step);
            previous = nodes_[i];
        }
        return true;
    }

    int NodeCount() const { return node_count_; }
    float Wave() const { return wave_; }

    Vec3 NodeWorld(int index) const {
        return index >= 0 && index < node_count_ ? FromOgAxes(nodes_[index]) : Vec3{};
    }

    Vec3 NodeOg(int index) const {
        return index >= 0 && index < node_count_ ? nodes_[index] : Vec3{};
    }

private:
    static Vec3 Add(const Vec3& a, const Vec3& b) {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    static Vec3 Sub(const Vec3& a, const Vec3& b) {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    static Vec3 Scale(const Vec3& v, float amount) {
        return {v.x * amount, v.y * amount, v.z * amount};
    }
    static float Dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    static float Length(const Vec3& v) {
        return std::sqrt(Dot(v, v));
    }
    static float Lerp(float a, float b, float amount) {
        return a + (b - a) * amount;
    }

    std::array<Vec3, kMaxNodes> nodes_{};
    int node_count_ = 0;
    float wave_ = 0.0f;
};

}  // namespace madeline_cube
