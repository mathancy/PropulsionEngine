#pragma once

/**
 * CollisionSettings.h
 * What an Object asset says about collision: whether other things bump into it, and whether it is a character that
 * stands on the ground, falls and jumps (Unreal's CharacterMovement component with its capsule).
 *
 * Every Object with a mesh is solid by default: its mesh's shape blocks characters (see physics/Collision.h). Turn
 * `solid` off for things a character should walk through, like a pickup.
 *
 * A character's capsule is centered on the Object's origin, upright, and scales with the Object's scale (width with
 * the larger of X and Y, height with Z), so it stays the same size as the mesh drawn around it.
 */

#include "physics/Collision.h"

#include <cmath>
#include <glm/glm.hpp>
#include <string>

namespace Propulsion {

struct CharacterMovementSettings {
    bool enabled = false;            // does the Object have the component at all
    float radius = 0.5f;             // the capsule's width, as a radius
    float halfHeight = 1.0f;         // half of its height, to the top of the rounded cap (never less than the radius)
    float gravity = 20.0f;           // units per second per second
    float jumpSpeed = 8.0f;          // upward speed of a jump (the jump rises jumpSpeed squared over twice gravity)
    float maxSlopeDegrees = 45.0f;   // steeper than this is a wall, not ground
    std::string jumpAction = "jump"; // the Input Action (a button) that makes it jump, from the Object's Input Mapping; empty for none

    static constexpr float kMinRadius = 0.01f;
    static constexpr float kMaxSize = 10000.0f;

    void sanitize() {
        auto size = [](float& v, float low) {
            if (!(v >= low)) v = low;
            if (v > kMaxSize) v = kMaxSize;
        };
        size(radius, kMinRadius);
        size(halfHeight, kMinRadius);
        if (halfHeight < radius) halfHeight = radius;
        if (!(gravity >= 0.0f)) gravity = 0.0f;
        if (!(jumpSpeed >= 0.0f)) jumpSpeed = 0.0f;
        if (!(maxSlopeDegrees >= 0.0f)) maxSlopeDegrees = 0.0f;
        if (maxSlopeDegrees > 89.0f) maxSlopeDegrees = 89.0f;
    }

    /// The capsule for an instance of the Object at this scale.
    Physics::CharacterParams paramsAt(const glm::vec3& scale) const {
        Physics::CharacterParams p;
        const float width = (std::max)(std::fabs(scale.x), std::fabs(scale.y));
        p.radius = (std::max)(radius * width, kMinRadius);
        p.halfHeight = (std::max)(halfHeight * std::fabs(scale.z), p.radius);
        p.gravity = gravity;
        p.jumpSpeed = jumpSpeed;
        p.maxSlopeDegrees = maxSlopeDegrees;
        return p;
    }

    bool operator==(const CharacterMovementSettings& o) const {
        return enabled == o.enabled && radius == o.radius && halfHeight == o.halfHeight && gravity == o.gravity &&
               jumpSpeed == o.jumpSpeed && maxSlopeDegrees == o.maxSlopeDegrees && jumpAction == o.jumpAction;
    }
    bool operator!=(const CharacterMovementSettings& o) const { return !(*this == o); }
};

struct CollisionSettings {
    bool solid = true;                    // characters can't walk through it
    CharacterMovementSettings character;  // a separate component in the editor

    void sanitize() { character.sanitize(); }

    bool operator==(const CollisionSettings& o) const { return solid == o.solid && character == o.character; }
    bool operator!=(const CollisionSettings& o) const { return !(*this == o); }
};

} // namespace Propulsion
