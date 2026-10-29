#pragma once

/**
 * CollisionOutline.h
 * The wireframe drawn for a solid shape by View > Collision Shapes: what characters actually bump into, laid over the
 * mesh. Header-only and free of Vulkan and ImGui so it can be tested on its own (tests/collision_outline_test.cpp).
 *
 * Each shape becomes line segments in world space, built from the same Shape the physics uses, so the outline is
 * exactly the thing that blocks you:
 *   Box       its twelve edges
 *   Sphere    three circles, one around each axis
 *   Cylinder  a circle at each end and four lines joining them (its length runs along its own Y axis)
 *   Capsule   a circle at each end of the straight part, four lines joining them, and the two rounded caps (its own Z axis)
 */

#include "core/CameraFrustum.h"  // Segment
#include "physics/Collision.h"

#include <cmath>
#include <vector>

namespace Propulsion {
namespace Physics {

/// How many straight pieces make a full circle in an outline.
inline constexpr int kOutlineCircleSegments = 24;

namespace detail {

/// Appends the pieces of an arc in the plane spanned by unit vectors `u` and `v`, around `center`, from `from` to `to`
/// (radians, measured from `u` toward `v`). Points are in the shape's own space; `place` moves them into the world.
template <typename Place>
inline void arcSegments(std::vector<Segment>& out, const Place& place, const glm::vec3& center, const glm::vec3& u,
                        const glm::vec3& v, float radius, float from, float to, int pieces) {
    glm::vec3 previous = place(center + (u * std::cos(from) + v * std::sin(from)) * radius);
    for (int i = 1; i <= pieces; ++i) {
        const float angle = from + (to - from) * static_cast<float>(i) / static_cast<float>(pieces);
        const glm::vec3 next = place(center + (u * std::cos(angle) + v * std::sin(angle)) * radius);
        out.push_back({previous, next});
        previous = next;
    }
}

}  // namespace detail

/// The outline of `s` as line segments in world space.
inline std::vector<Segment> shapeOutline(const Shape& s) {
    std::vector<Segment> out;
    const auto place = [&](const glm::vec3& local) { return s.center + s.rotation * local; };
    const glm::vec3 X(1.0f, 0.0f, 0.0f), Y(0.0f, 1.0f, 0.0f), Z(0.0f, 0.0f, 1.0f);
    const float kPi = 3.14159265358979f;
    const float kTwoPi = 2.0f * kPi;
    const int n = kOutlineCircleSegments;
    const glm::vec3 zero(0.0f);

    switch (s.kind) {
        case ShapeKind::Box: {
            glm::vec3 corner[8];
            for (int i = 0; i < 8; ++i) {
                corner[i] = place(glm::vec3((i & 1) ? s.half.x : -s.half.x, (i & 2) ? s.half.y : -s.half.y, (i & 4) ? s.half.z : -s.half.z));
            }
            for (int i = 0; i < 8; ++i) {
                for (int bit = 1; bit <= 4; bit <<= 1) {
                    if (!(i & bit)) out.push_back({corner[i], corner[i | bit]});  // each edge once: from its low end
                }
            }
            break;
        }
        case ShapeKind::Sphere:
            detail::arcSegments(out, place, zero, X, Y, s.half.x, 0.0f, kTwoPi, n);
            detail::arcSegments(out, place, zero, Y, Z, s.half.x, 0.0f, kTwoPi, n);
            detail::arcSegments(out, place, zero, Z, X, s.half.x, 0.0f, kTwoPi, n);
            break;
        case ShapeKind::Cylinder: {
            const float r = s.half.x, h = s.half.y;
            detail::arcSegments(out, place, Y * -h, X, Z, r, 0.0f, kTwoPi, n);
            detail::arcSegments(out, place, Y * h, X, Z, r, 0.0f, kTwoPi, n);
            const glm::vec3 spokes[4] = {X * r, X * -r, Z * r, Z * -r};
            for (const glm::vec3& spoke : spokes) out.push_back({place(spoke - Y * h), place(spoke + Y * h)});
            break;
        }
        case ShapeKind::Capsule: {
            const float r = s.half.x;
            const float straight = (std::max)(0.0f, s.half.y - r);  // half the length of the part between the caps
            detail::arcSegments(out, place, Z * -straight, X, Y, r, 0.0f, kTwoPi, n);
            detail::arcSegments(out, place, Z * straight, X, Y, r, 0.0f, kTwoPi, n);
            const glm::vec3 spokes[4] = {X * r, X * -r, Y * r, Y * -r};
            for (const glm::vec3& spoke : spokes) out.push_back({place(spoke - Z * straight), place(spoke + Z * straight)});
            // The caps: half a circle each, in the XZ and YZ planes, bulging away from the middle.
            detail::arcSegments(out, place, Z * straight, X, Z, r, 0.0f, kPi, n / 2);
            detail::arcSegments(out, place, Z * straight, Y, Z, r, 0.0f, kPi, n / 2);
            detail::arcSegments(out, place, Z * -straight, X, Z, r, kPi, kTwoPi, n / 2);
            detail::arcSegments(out, place, Z * -straight, Y, Z, r, kPi, kTwoPi, n / 2);
            break;
        }
    }
    return out;
}

}  // namespace Physics
}  // namespace Propulsion
