#pragma once

/**
 * Collision.h
 * Solid shapes for the objects in a scene, and a character mover that walks, falls and jumps among them.
 * Header-only so it can be tested without a window (tests/collision_test.cpp).
 *
 * Every scene object with a mesh is one solid shape: its mesh's bounds, turned and scaled with the object.
 *   Box       the bounds of any mesh that isn't one of the two below (a cube is exact)
 *   Sphere    the Sphere mesh (a ball as wide as the object's largest scale)
 *   Cylinder  the Cylinder mesh (its length runs along the mesh's own Y axis, as drawn)
 *
 * A character is an upright capsule (the pill shape every game character uses; Unreal's capsule component) centered on
 * the object's origin. `moveCharacter` does what a CharacterMovement component does each frame: it takes the move the
 * script asked for, adds gravity and a jump, and slides the capsule along whatever is in the way.
 *
 * Z is up. Gravity pulls along -Z.
 */

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Propulsion {
namespace Physics {

enum class ShapeKind { Box, Sphere, Cylinder, Capsule };

struct Shape {
    ShapeKind kind = ShapeKind::Box;
    glm::vec3 center{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    /// Box: the half-size along each of its own axes. Sphere: the radius is half.x.
    /// Cylinder: the radius is half.x, and half.y is half its length, along its own Y axis.
    /// Capsule: the radius is half.x, and half.y is half its whole height (to the top of its cap), along its own Z axis.
    glm::vec3 half{0.5f};
    int id = -1;  // whatever the caller wants to know it by (a scene index, say)

    /// A ball that surrounds the whole shape, for ruling most shapes out cheaply.
    float boundingRadius() const {
        switch (kind) {
            case ShapeKind::Sphere: return half.x;
            case ShapeKind::Cylinder: return std::sqrt(half.x * half.x + half.y * half.y);
            case ShapeKind::Capsule: return (std::max)(half.x, half.y);
            default: return glm::length(half);
        }
    }
};

/// An upright capsule: `halfHeight` is measured to the top of its rounded end, like a character's.
inline Shape capsuleShape(const glm::vec3& center, float radius, float halfHeight, int id = -1) {
    Shape s;
    s.kind = ShapeKind::Capsule;
    s.center = center;
    s.half = glm::vec3(radius, (std::max)(radius, halfHeight), radius);
    s.id = id;
    return s;
}

/// The shape of an object whose mesh spans `boundsMin`..`boundsMax` before the object's own scale, rotation and position.
inline Shape shapeFromMesh(ShapeKind kind, const glm::vec3& boundsMin, const glm::vec3& boundsMax, const glm::vec3& position,
                           const glm::quat& rotation, const glm::vec3& scale, int id = -1) {
    Shape s;
    s.kind = kind;
    s.id = id;
    s.rotation = rotation;
    s.center = position + rotation * (((boundsMin + boundsMax) * 0.5f) * scale);
    s.half = ((boundsMax - boundsMin) * 0.5f) * glm::abs(scale);
    if (kind == ShapeKind::Sphere) {
        s.half = glm::vec3((std::max)((std::max)(s.half.x, s.half.y), s.half.z));
    } else if (kind == ShapeKind::Cylinder) {
        const float radius = (std::max)(s.half.x, s.half.z);
        s.half = glm::vec3(radius, s.half.y, radius);
    }
    return s;
}

/// Whether a ball (center `c`, radius `r`) overlaps the shape. If it does, `normal` is the way to move the ball out
/// (away from the shape) and `depth` how far that is.
inline bool pushOutOfShape(const Shape& s, const glm::vec3& c, float r, glm::vec3& normal, float& depth) {
    const glm::quat inverse = glm::conjugate(s.rotation);
    const glm::vec3 l = inverse * (c - s.center);  // the ball's center in the shape's own frame
    glm::vec3 n(0.0f, 0.0f, 1.0f);
    float d = 0.0f;

    switch (s.kind) {
        case ShapeKind::Box: {
            const glm::vec3 nearest = glm::clamp(l, -s.half, s.half);
            const glm::vec3 away = l - nearest;
            const float dist2 = glm::dot(away, away);
            if (dist2 > 1e-12f) {
                const float dist = std::sqrt(dist2);
                if (dist >= r) return false;
                n = away / dist;
                d = r - dist;
            } else {  // the center is inside: leave by the nearest face
                const glm::vec3 room = s.half - glm::abs(l);
                const int axis = room.x <= room.y ? (room.x <= room.z ? 0 : 2) : (room.y <= room.z ? 1 : 2);
                n = glm::vec3(0.0f);
                n[axis] = l[axis] >= 0.0f ? 1.0f : -1.0f;
                d = r + room[axis];
            }
            break;
        }
        case ShapeKind::Sphere: {
            const float len = glm::length(l);
            if (len >= s.half.x + r) return false;
            n = len > 1e-6f ? l / len : inverse * glm::vec3(0.0f, 0.0f, 1.0f);
            d = s.half.x + r - len;
            break;
        }
        case ShapeKind::Capsule: {
            const float segment = (std::max)(0.0f, s.half.y - s.half.x);
            const glm::vec3 nearest(0.0f, 0.0f, glm::clamp(l.z, -segment, segment));
            const glm::vec3 away = l - nearest;
            const float dist = glm::length(away);
            if (dist >= s.half.x + r) return false;
            n = dist > 1e-6f ? away / dist : glm::vec3(0.0f, 0.0f, 1.0f);
            d = s.half.x + r - dist;
            break;
        }
        case ShapeKind::Cylinder: {
            const glm::vec2 radial(l.x, l.z);
            const float rl = glm::length(radial);
            const float radius = s.half.x, halfLength = s.half.y;
            const glm::vec2 dir = rl > 1e-6f ? radial / rl : glm::vec2(1.0f, 0.0f);
            if (rl <= radius && std::fabs(l.y) <= halfLength) {  // inside: leave by the nearest of the side and the caps
                const float toSide = radius - rl, toCap = halfLength - std::fabs(l.y);
                if (toCap <= toSide) {
                    n = glm::vec3(0.0f, l.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
                    d = r + toCap;
                } else {
                    n = glm::vec3(dir.x, 0.0f, dir.y);
                    d = r + toSide;
                }
            } else {
                const float cr = (std::min)(rl, radius);
                const glm::vec3 nearest(dir.x * cr, glm::clamp(l.y, -halfLength, halfLength), dir.y * cr);
                const glm::vec3 away = l - nearest;
                const float dist = glm::length(away);
                if (dist >= r || dist <= 0.0f) return false;
                n = away / dist;
                d = r - dist;
            }
            break;
        }
    }
    normal = s.rotation * n;
    depth = d;
    return true;
}

/// How far a point is from the shape: negative inside it, zero on its surface.
inline float signedDistance(const Shape& s, const glm::vec3& p) {
    const glm::vec3 l = glm::conjugate(s.rotation) * (p - s.center);
    switch (s.kind) {
        case ShapeKind::Sphere: return glm::length(l) - s.half.x;
        case ShapeKind::Capsule: {
            const float segment = (std::max)(0.0f, s.half.y - s.half.x);
            return glm::length(l - glm::vec3(0.0f, 0.0f, glm::clamp(l.z, -segment, segment))) - s.half.x;
        }
        case ShapeKind::Cylinder: {
            const glm::vec2 q(glm::length(glm::vec2(l.x, l.z)) - s.half.x, std::fabs(l.y) - s.half.y);
            return (std::min)((std::max)(q.x, q.y), 0.0f) + glm::length(glm::max(q, glm::vec2(0.0f)));
        }
        default: {
            const glm::vec3 q = glm::abs(l) - s.half;
            return glm::length(glm::max(q, glm::vec3(0.0f))) + (std::min)((std::max)(q.x, (std::max)(q.y, q.z)), 0.0f);
        }
    }
}

/// What a trace found.
struct TraceHit {
    bool blocked = false;
    int id = -1;               // the `id` of the shape it hit
    glm::vec3 location{0.0f};  // where the line met the shape's surface (or its end, if it hit nothing)
    glm::vec3 normal{0.0f};    // which way the surface faces there
    float distance = 0.0f;     // how far along the line, from `from`
};

/// What a line from `from` to `to`, this thick (a ball of `radius` slid along it), meets first. If it starts inside a
/// shape, the hit is at distance 0.
inline TraceHit traceShapes(const std::vector<Shape>& shapes, const glm::vec3& from, const glm::vec3& to, float radius = 0.0f) {
    TraceHit result;
    result.location = to;
    const glm::vec3 line = to - from;
    const float length = glm::length(line);
    result.distance = length;
    if (!(length > 0.0f) || shapes.empty()) return result;
    const glm::vec3 dir = line / length;
    radius = (std::max)(0.0f, radius);

    // Sphere tracing: the distance to the nearest shape is how far the ball can safely travel.
    float t = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        const glm::vec3 p = from + dir * t;
        const Shape* nearest = nullptr;
        float nearestDistance = 0.0f;
        for (const Shape& s : shapes) {
            const float d = signedDistance(s, p);
            if (!nearest || d < nearestDistance) {
                nearest = &s;
                nearestDistance = d;
            }
        }
        const float gap = nearestDistance - radius;
        if (gap < 1e-3f) {
            // Which way the surface faces: the way the distance grows.
            const float e = 1e-3f;
            glm::vec3 gradient(signedDistance(*nearest, p + glm::vec3(e, 0, 0)) - signedDistance(*nearest, p - glm::vec3(e, 0, 0)),
                               signedDistance(*nearest, p + glm::vec3(0, e, 0)) - signedDistance(*nearest, p - glm::vec3(0, e, 0)),
                               signedDistance(*nearest, p + glm::vec3(0, 0, e)) - signedDistance(*nearest, p - glm::vec3(0, 0, e)));
            const float gl = glm::length(gradient);
            result.blocked = true;
            result.id = nearest->id;
            result.normal = gl > 1e-6f ? gradient / gl : -dir;
            result.location = p - result.normal * nearestDistance;
            result.distance = t;
            return result;
        }
        t += (std::max)(gap, 0.01f);  // (at least this much, so a line skimming a surface still gets somewhere)
        if (t > length) break;
    }
    return result;
}

namespace detail {

/// The farthest point of a shape (grown by `grow`) in a direction, for GJK.
inline glm::vec3 supportPoint(const Shape& s, const glm::vec3& dir, float grow) {
    const glm::vec3 d = glm::conjugate(s.rotation) * dir;
    glm::vec3 local(0.0f);
    switch (s.kind) {
        case ShapeKind::Sphere: {
            const float len = glm::length(d);
            local = len > 1e-9f ? d / len * s.half.x : glm::vec3(0.0f);
            break;
        }
        case ShapeKind::Capsule: {
            const float len = glm::length(d);
            const float segment = (std::max)(0.0f, s.half.y - s.half.x);
            local = glm::vec3(0.0f, 0.0f, d.z >= 0.0f ? segment : -segment) + (len > 1e-9f ? d / len * s.half.x : glm::vec3(0.0f));
            break;
        }
        case ShapeKind::Cylinder: {
            const float radial = std::sqrt(d.x * d.x + d.z * d.z);
            local = glm::vec3(radial > 1e-9f ? d.x / radial * s.half.x : 0.0f, d.y >= 0.0f ? s.half.y : -s.half.y,
                              radial > 1e-9f ? d.z / radial * s.half.x : 0.0f);
            break;
        }
        default:
            local = glm::vec3(d.x >= 0.0f ? s.half.x : -s.half.x, d.y >= 0.0f ? s.half.y : -s.half.y, d.z >= 0.0f ? s.half.z : -s.half.z);
            break;
    }
    glm::vec3 point = s.center + s.rotation * local;
    if (grow > 0.0f) {
        const float len = glm::length(dir);
        if (len > 1e-9f) point += dir / len * grow;
    }
    return point;
}

inline bool sameDirection(const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b) > 0.0f; }

/// One step of GJK: keep the part of the simplex nearest the origin and say which way to look next.
/// Returns true if the origin is inside the simplex (the shapes overlap). The newest point is first.
inline bool nextSimplex(std::vector<glm::vec3>& p, glm::vec3& dir) {
    auto line = [&]() {
        const glm::vec3 a = p[0], b = p[1];
        const glm::vec3 ab = b - a, ao = -a;
        if (sameDirection(ab, ao)) {
            dir = glm::cross(glm::cross(ab, ao), ab);
        } else {
            p = {a};
            dir = ao;
        }
    };
    auto triangle = [&]() {
        const glm::vec3 a = p[0], b = p[1], c = p[2];
        const glm::vec3 ab = b - a, ac = c - a, ao = -a;
        const glm::vec3 abc = glm::cross(ab, ac);
        if (sameDirection(glm::cross(abc, ac), ao)) {
            if (sameDirection(ac, ao)) {
                p = {a, c};
                dir = glm::cross(glm::cross(ac, ao), ac);
            } else {
                p = {a, b};
                line();
            }
        } else if (sameDirection(glm::cross(ab, abc), ao)) {
            p = {a, b};
            line();
        } else if (sameDirection(abc, ao)) {
            dir = abc;
        } else {
            p = {a, c, b};
            dir = -abc;
        }
    };
    switch (p.size()) {
        case 2: line(); return false;
        case 3: triangle(); return false;
        default: {
            const glm::vec3 a = p[0], b = p[1], c = p[2], d = p[3];
            const glm::vec3 ab = b - a, ac = c - a, ad = d - a, ao = -a;
            if (sameDirection(glm::cross(ab, ac), ao)) { p = {a, b, c}; triangle(); return false; }
            if (sameDirection(glm::cross(ac, ad), ao)) { p = {a, c, d}; triangle(); return false; }
            if (sameDirection(glm::cross(ad, ab), ao)) { p = {a, d, b}; triangle(); return false; }
            return true;
        }
    }
}

} // namespace detail

/// Whether two shapes overlap, or come within `slop` of each other (so things resting against each other count).
/// Works for any two of the shapes (GJK: it only needs each shape's farthest point in a direction).
inline bool shapesOverlap(const Shape& a, const Shape& b, float slop = 0.0f) {
    const float grow = (std::max)(0.0f, slop);
    auto support = [&](const glm::vec3& d) { return detail::supportPoint(a, d, grow) - detail::supportPoint(b, -d, 0.0f); };

    glm::vec3 dir = b.center - a.center;
    if (glm::dot(dir, dir) < 1e-12f) dir = glm::vec3(1.0f, 0.0f, 0.0f);
    std::vector<glm::vec3> simplex;
    glm::vec3 point = support(dir);
    simplex.push_back(point);
    dir = -point;
    for (int i = 0; i < 64; ++i) {
        if (glm::dot(dir, dir) < 1e-12f) return true;  // the origin is on the simplex: touching
        point = support(dir);
        if (glm::dot(point, dir) < 0.0f) return false;  // that way is a gap
        simplex.insert(simplex.begin(), point);
        if (detail::nextSimplex(simplex, dir)) return true;
    }
    return false;
}

/// The size of a character: an upright capsule centered on the object's origin. `halfHeight` is measured to the top
/// of the rounded cap, so the whole capsule is `2 * halfHeight` tall (and never narrower than `2 * radius`).
struct CharacterParams {
    float radius = 0.5f;
    float halfHeight = 1.0f;
    float gravity = 20.0f;         // units per second per second, pulling down
    float jumpSpeed = 8.0f;        // the upward speed a jump starts with
    float maxSlopeDegrees = 45.0f; // steeper than this is a wall, not ground
};

struct CharacterState {
    glm::vec3 position{0.0f};
    float velocityZ = 0.0f;  // falling is negative
    bool grounded = false;   // standing on something walkable
};

/// How far under the feet still counts as standing on something.
inline constexpr float kGroundReach = 0.05f;
/// The fastest a character falls.
inline constexpr float kTerminalSpeed = 60.0f;

namespace detail {

/// The balls that stand in for a capsule: spaced no further apart than one radius, from the bottom cap to the top.
inline int capsuleBalls(const CharacterParams& p, float& spacing, float& lowest) {
    const float segment = (std::max)(0.0f, p.halfHeight - p.radius);
    lowest = -segment;
    if (segment <= 1e-5f) {
        spacing = 0.0f;
        return 1;
    }
    const int count = static_cast<int>(std::ceil(2.0f * segment / p.radius)) + 1;
    spacing = 2.0f * segment / static_cast<float>(count - 1);
    return count;
}

} // namespace detail

/// Move a character for one frame. `wanted` is how far the script moved it (what `move Self forward ...` did), and
/// `jump` is whether the jump button went down this frame. The capsule is carried along that move plus gravity and
/// slides along anything in the way, so it walks along the ground, stops at walls, and falls off edges.
/// `touched`, when given, gets the `id` of every shape that blocked the capsule.
inline void moveCharacter(CharacterState& state, const CharacterParams& params, const glm::vec3& wanted, bool jump, float dt,
                          const std::vector<Shape>& world, std::vector<int>* touched = nullptr) {
    const float cosSlope = std::cos(glm::radians(params.maxSlopeDegrees));
    float spacing = 0.0f, lowest = 0.0f;
    const int balls = detail::capsuleBalls(params, spacing, lowest);

    auto note = [&](int id) {
        if (!touched || id < 0) return;
        if (std::find(touched->begin(), touched->end(), id) == touched->end()) touched->push_back(id);
    };

    // Move out of everything the capsule overlaps. Ground is left by going straight up (so standing on a slope
    // doesn't slowly slide down it); anything else, like a wall, is left along its surface normal (so the capsule
    // slides along it).
    auto resolve = [&](glm::vec3& position, float& velocityZ) {
        for (int pass = 0; pass < 4; ++pass) {
            bool moved = false;
            for (int i = 0; i < balls; ++i) {
                for (const Shape& shape : world) {
                    const glm::vec3 c = position + glm::vec3(0.0f, 0.0f, lowest + spacing * static_cast<float>(i));
                    if (glm::distance(c, shape.center) > shape.boundingRadius() + params.radius) continue;
                    glm::vec3 normal;
                    float depth;
                    if (!pushOutOfShape(shape, c, params.radius, normal, depth)) continue;
                    if (normal.z >= cosSlope) {
                        position.z += depth / normal.z;
                        if (velocityZ < 0.0f) velocityZ = 0.0f;
                    } else {
                        position += normal * depth;
                        if (normal.z < -cosSlope && velocityZ > 0.0f) velocityZ = 0.0f;  // bumped a ceiling
                    }
                    note(shape.id);
                    moved = true;
                }
            }
            if (!moved) break;
        }
    };

    auto standingOn = [&](const glm::vec3& position) {
        const glm::vec3 probe = position - glm::vec3(0.0f, 0.0f, kGroundReach);
        for (int i = 0; i < balls; ++i) {
            for (const Shape& shape : world) {
                const glm::vec3 c = probe + glm::vec3(0.0f, 0.0f, lowest + spacing * static_cast<float>(i));
                if (glm::distance(c, shape.center) > shape.boundingRadius() + params.radius) continue;
                glm::vec3 normal;
                float depth;
                if (pushOutOfShape(shape, c, params.radius, normal, depth) && normal.z >= cosSlope) return true;
            }
        }
        return false;
    };

    if (jump && state.grounded) state.velocityZ = params.jumpSpeed;
    state.velocityZ = (std::max)(state.velocityZ - params.gravity * dt, -kTerminalSpeed);

    // Small steps, so a fast capsule can't jump over a thin wall.
    const glm::vec3 total = wanted + glm::vec3(0.0f, 0.0f, state.velocityZ * dt);
    const float reach = (std::max)(0.05f, params.radius * 0.5f);
    const int steps = (std::min)(100, (std::max)(1, static_cast<int>(std::ceil(glm::length(total) / reach))));
    const glm::vec3 each = total / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        state.position += each;
        resolve(state.position, state.velocityZ);
    }

    state.grounded = state.velocityZ <= 0.0f && standingOn(state.position);
}

} // namespace Physics
} // namespace Propulsion
