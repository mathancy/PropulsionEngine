#pragma once

/**
 * LightSettings.h
 * The Light component an Object asset can carry: a directional light (the sun) or a point light (a bulb).
 *
 * An Object with a light shines from where it stands. A directional light shines along the Object's forward
 * direction (+Y, the same way a camera looks), so rotating the Object aims the sun; its position doesn't matter.
 * A point light shines in every direction from the Object's position.
 *
 * Conventions (the engine's world): Z is up, forward is +Y, right is +X (right-handed, Blender-style).
 *
 * Units: `intensity` multiplies `color`. For a directional light it is the brightness of the light arriving at a
 * surface that faces it (3 is a bright sunny day in the default exposure). For a point light it is the brightness at
 * a distance of one unit, falling off with the square of the distance (so 20 is bright at 2 units, dim at 10).
 */

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Propulsion {

struct LightSettings {
    enum class Type : uint8_t { Directional = 0, Point = 1 };

    bool enabled = false;               // does the Object have a light component at all
    Type type = Type::Point;
    glm::vec3 color{1.0f, 1.0f, 1.0f};  // linear RGB
    float intensity = 20.0f;
    float range = 25.0f;                // point: the distance where its light has faded to nothing
    float radius = 0.1f;                // point: the size of the bulb. Used for soft shadows by the ray-traced modes
    float angularDiameterDeg = 0.53f;   // directional: how big the sun looks. Used for soft shadows by the ray-traced modes
    bool castShadows = true;            // used by the ray-traced modes

    static constexpr float kMaxIntensity = 100000.0f;
    static constexpr float kMaxRange = 100000.0f;
    static constexpr float kMinRange = 0.01f;
    static constexpr float kMaxRadius = 1000.0f;
    static constexpr float kMaxAngularDiameter = 90.0f;

    /// What a new directional light is: a warm sun, as bright as the scene's old built-in key light.
    static LightSettings directional() {
        LightSettings l;
        l.enabled = true;
        l.type = Type::Directional;
        l.color = glm::vec3(1.0f, 0.98f, 0.95f);
        l.intensity = 3.0f;
        return l;
    }

    /// What a new point light is: a white bulb that reaches 25 units.
    static LightSettings point() {
        LightSettings l;
        l.enabled = true;
        l.type = Type::Point;
        return l;
    }

    /// Pull every value back into a range the renderer can use (a damaged file, or a field dragged too far).
    void sanitize() {
        if (type != Type::Directional && type != Type::Point) type = Type::Point;
        for (int i = 0; i < 3; i++) {
            if (!(color[i] >= 0.0f)) color[i] = 0.0f;
            if (color[i] > 1000.0f) color[i] = 1000.0f;
        }
        if (!(intensity >= 0.0f)) intensity = 0.0f;
        if (intensity > kMaxIntensity) intensity = kMaxIntensity;
        if (!(range >= kMinRange)) range = kMinRange;
        if (range > kMaxRange) range = kMaxRange;
        if (!(radius >= 0.0f)) radius = 0.0f;
        if (radius > kMaxRadius) radius = kMaxRadius;
        if (!(angularDiameterDeg >= 0.0f)) angularDiameterDeg = 0.0f;
        if (angularDiameterDeg > kMaxAngularDiameter) angularDiameterDeg = kMaxAngularDiameter;
    }

    bool operator==(const LightSettings& o) const {
        return enabled == o.enabled && type == o.type && color == o.color && intensity == o.intensity && range == o.range &&
               radius == o.radius && angularDiameterDeg == o.angularDiameterDeg && castShadows == o.castShadows;
    }
    bool operator!=(const LightSettings& o) const { return !(*this == o); }
};

/// The direction a light's rays travel: the Object's forward (+Y) turned by its rotation.
inline glm::vec3 lightTravelDirection(const glm::quat& objectRotation) {
    return glm::normalize(objectRotation * glm::vec3(0.0f, 1.0f, 0.0f));
}

/// The rotation that turns an Object's forward (+Y) to point along `direction`: how to aim a directional light.
inline glm::quat rotationFromForwardTo(const glm::vec3& direction) {
    const glm::vec3 from(0.0f, 1.0f, 0.0f);
    const glm::vec3 to = glm::normalize(direction);
    const float cosine = glm::dot(from, to);
    if (cosine > 0.99999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (cosine < -0.99999f) return glm::angleAxis(3.14159265358979f, glm::vec3(0.0f, 0.0f, 1.0f));  // straight back: half a turn about up
    return glm::angleAxis(std::acos(cosine), glm::normalize(glm::cross(from, to)));
}

} // namespace Propulsion
