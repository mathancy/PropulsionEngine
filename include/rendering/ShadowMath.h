#pragma once

/**
 * ShadowMath.h
 * The geometry behind shadow maps, with no GPU calls (tested in tests/shadow_test.cpp):
 *   - the sun's shadow map: one orthographic map fitted around the part of the view that is close enough to matter,
 *     snapped to whole texels so shadow edges don't crawl when the camera moves or turns;
 *   - a point light's shadow: six 90 degree views (one per axis direction), and which of them a direction falls in.
 * The shaders (shaders/lighting.glsl) use the same matrices and the same face choice.
 *
 * Conventions (the engine's world): Z is up, forward is +Y, right is +X. Depth is 0 (near) to 1 (far).
 */

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Propulsion {
namespace Shadows {

constexpr int kMaxPointShadows = 4;      // how many point lights can cast shadows at once
constexpr int kCascadeCount = 4;         // the sun's shadow is this many maps, each covering a deeper slice of the view
constexpr int kCascadeMapSize = 2048;    // each of the sun's maps
constexpr float kCascadeSplitBlend = 0.15f;  // each map fades into the next over this fraction of its depth range
constexpr float kCascadeFirstReach = 1.0f;   // the maps' reaches grow from about this far in front of the camera
constexpr int kPointMapSize = 1024;      // each of a point light's six faces
constexpr int kFacesPerPoint = 6;
constexpr float kPointNearPlane = 0.05f;

/// A box in the world: what the shadow casters take up.
struct Aabb {
    glm::vec3 min{FLT_MAX};
    glm::vec3 max{-FLT_MAX};

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    void expand(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void expand(const Aabb& o) {
        if (!o.valid()) return;
        expand(o.min);
        expand(o.max);
    }
    /// The box of a local-space box moved into the world by `model`.
    static Aabb transformed(const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& model) {
        Aabb box;
        for (int i = 0; i < 8; i++) {
            const glm::vec3 corner((i & 1) ? localMax.x : localMin.x, (i & 2) ? localMax.y : localMin.y, (i & 4) ? localMax.z : localMin.z);
            box.expand(glm::vec3(model * glm::vec4(corner, 1.0f)));
        }
        return box;
    }
};

/// The eight corners (world space) of the part of a camera's view between two distances from the camera along its view
/// direction. Works for any projection that maps the screen to -1..1 (it only uses the directions of the corner rays).
inline std::array<glm::vec3, 8> frustumSliceCorners(const glm::mat4& view, const glm::mat4& projection, float nearDistance, float farDistance) {
    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    const glm::mat4 inverseView = glm::inverse(view);
    const glm::vec3 eye(inverseView[3]);
    const glm::vec3 forward = glm::normalize(-glm::vec3(inverseView[2]));  // a view looks down its -Z

    std::array<glm::vec3, 8> corners{};
    int n = 0;
    for (float distance : {nearDistance, farDistance}) {
        for (int i = 0; i < 4; i++) {
            const float x = (i & 1) ? 1.0f : -1.0f;
            const float y = (i & 2) ? 1.0f : -1.0f;
            // Two points on the ray through this corner: its direction (the depth values don't matter, only that they differ).
            const glm::vec4 a = inverseViewProjection * glm::vec4(x, y, 0.25f, 1.0f);
            const glm::vec4 b = inverseViewProjection * glm::vec4(x, y, 0.75f, 1.0f);
            glm::vec3 direction = glm::normalize(glm::vec3(b) / b.w - glm::vec3(a) / a.w);
            if (glm::dot(direction, forward) < 0.0f) direction = -direction;  // always away from the camera
            corners[n++] = eye + direction * (distance / glm::dot(direction, forward));
        }
    }
    return corners;
}

struct DirectionalShadow {
    bool valid = false;
    glm::mat4 viewProjection{1.0f};  // world to the sun's clip space: x, y in -1..1, z in 0..1
    float depthRange = 1.0f;         // how many world units depth 0 to 1 covers
    float orthoWidth = 1.0f;         // how many world units the map is wide
};

/// Fit the sun's shadow map around a slice of the view. `lightTravel` is the way the light shines. `casters` is what can
/// cast shadows (it stretches the map toward the sun so shadows from things outside the view still fall inside it).
/// The map is square and its size depends only on the slice's size (not how it is turned), and its position moves in whole
/// texels, so a shadow edge stays put on the ground as the camera moves and turns.
inline DirectionalShadow fitDirectionalShadow(const std::array<glm::vec3, 8>& corners, const glm::vec3& lightTravel, const Aabb& casters, int mapSize) {
    DirectionalShadow result;
    if (mapSize <= 0 || glm::length(lightTravel) < 1e-6f) return result;

    const glm::vec3 direction = glm::normalize(lightTravel);
    const glm::vec3 up = std::abs(direction.z) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::mat4 view = glm::lookAtRH(glm::vec3(0.0f), direction, up);  // looks along the light, from the world's origin

    // A sphere around the slice, centered on the middle of its corners: the same size however the camera is turned.
    glm::vec3 center(0.0f);
    for (const glm::vec3& c : corners) center += c;
    center /= 8.0f;
    float radius = 0.0f;
    for (const glm::vec3& c : corners) radius = (std::max)(radius, glm::length(c - center));
    radius = (std::max)(radius, 0.01f);

    // Move the center to a whole number of texels (in the sun's own x and y).
    glm::vec3 centerInLight = glm::vec3(view * glm::vec4(center, 1.0f));
    const float texel = 2.0f * radius / static_cast<float>(mapSize);
    centerInLight.x = std::floor(centerInLight.x / texel) * texel;
    centerInLight.y = std::floor(centerInLight.y / texel) * texel;

    // In the sun's view things in front of it have negative z. The map must reach back toward the sun to the farthest
    // caster that could shade the slice, and forward to the far side of the slice.
    float zTowardLight = centerInLight.z + radius;
    const float zAwayFromLight = centerInLight.z - radius;
    if (casters.valid()) {
        for (int i = 0; i < 8; i++) {
            const glm::vec3 corner((i & 1) ? casters.max.x : casters.min.x, (i & 2) ? casters.max.y : casters.min.y, (i & 4) ? casters.max.z : casters.min.z);
            zTowardLight = (std::max)(zTowardLight, (view * glm::vec4(corner, 1.0f)).z);
        }
    }
    const float nearPlane = -zTowardLight;
    const float farPlane = -zAwayFromLight;

    const glm::mat4 projection = glm::orthoRH_ZO(centerInLight.x - radius, centerInLight.x + radius, centerInLight.y - radius,
                                                 centerInLight.y + radius, nearPlane, farPlane);
    result.valid = true;
    result.viewProjection = projection * view;
    result.depthRange = farPlane - nearPlane;
    result.orthoWidth = 2.0f * radius;
    return result;
}

// ---- Cascades ----------------------------------------------------------------------------------------------------------
// One map can't be sharp near the camera and reach far: its texels are as big close up as they are far away. So the sun
// gets several maps. The first covers just the stretch of the view in front of the camera and has tiny texels; each next
// one covers a deeper, wider stretch with bigger ones. A shadow is read from the nearest map that covers it, so shadows get
// sharper as you move toward them. (Each map is as tight a fit as `fitDirectionalShadow` makes it.)

/// How far from the camera (along its view direction) each map reaches: the last is `farDistance`. Each reaches a constant
/// factor farther than the one before, starting from about a unit in front of the camera: so each covers about the same
/// amount of the picture (a shadow's texels stay about as big as the screen's pixels), however far the shadows reach.
inline std::array<float, kCascadeCount> cascadeSplits(float nearDistance, float farDistance) {
    std::array<float, kCascadeCount> splits{};
    // Where the growth starts: about kCascadeFirstReach, but for a short view a quarter of it; never closer than the camera sees.
    const float closest = (std::max)((std::max)(nearDistance, 1e-3f), (std::min)(kCascadeFirstReach, farDistance * 0.25f));
    const float farthest = (std::max)(farDistance, closest * 1.01f);
    const float growth = std::pow(farthest / closest, 1.0f / static_cast<float>(kCascadeCount));
    float reach = closest;
    for (int i = 0; i < kCascadeCount; i++) {
        reach *= growth;
        splits[i] = reach;
    }
    splits[kCascadeCount - 1] = farthest;
    return splits;
}

/// How deep the zone is, at the far end of map `i`, where it fades into map `i + 1`.
inline float cascadeBlendWidth(const std::array<float, kCascadeCount>& splits, int i) {
    const float start = i > 0 ? splits[i - 1] : 0.0f;
    return kCascadeSplitBlend * (splits[i] - start);
}

struct CascadedShadow {
    bool valid = false;
    int count = 0;
    std::array<float, kCascadeCount> splitDepth{};   // how far from the camera each map reaches
    std::array<DirectionalShadow, kCascadeCount> maps{};
};

/// The sun's maps for a camera: `maps[i]` covers the view from just before `splitDepth[i - 1]` (so the fade between two
/// maps has both) to `splitDepth[i]`.
inline CascadedShadow fitCascadedShadow(const glm::mat4& view, const glm::mat4& projection, float nearDistance, float shadowDistance,
                                        const glm::vec3& lightTravel, const Aabb& casters, int mapSize) {
    CascadedShadow result;
    if (!(shadowDistance > nearDistance)) return result;
    result.splitDepth = cascadeSplits(nearDistance, shadowDistance);
    for (int i = 0; i < kCascadeCount; i++) {
        const float sliceNear = i == 0 ? nearDistance : result.splitDepth[i - 1] - cascadeBlendWidth(result.splitDepth, i - 1);
        const auto corners = frustumSliceCorners(view, projection, sliceNear, result.splitDepth[i]);
        result.maps[i] = fitDirectionalShadow(corners, lightTravel, casters, mapSize);
        if (!result.maps[i].valid) return CascadedShadow();
    }
    result.count = kCascadeCount;
    result.valid = true;
    return result;
}

/// Can something shaped like a sphere (around a caster) be seen by this map? (Its depth isn't checked: a map reaches back
/// toward the sun as far as the casters go.)
inline bool sphereOverlapsMap(const DirectionalShadow& map, const glm::vec3& center, float radius) {
    if (!map.valid) return false;
    const glm::vec4 clip = map.viewProjection * glm::vec4(center, 1.0f);  // orthographic: w is 1
    const float reach = 1.0f + 2.0f * radius / map.orthoWidth;
    return std::abs(clip.x) <= reach && std::abs(clip.y) <= reach;
}

// ---- Point lights ------------------------------------------------------------------------------------------------------

/// The axis a face of a point light's shadow looks along: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.
inline glm::vec3 pointFaceDirection(int face) {
    static const glm::vec3 directions[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    return directions[face];
}

/// Which face a direction from the light falls in: the axis it is most along (ties go to X, then Y). Same rule as the shader.
inline int pointFaceOf(const glm::vec3& direction) {
    const glm::vec3 a = glm::abs(direction);
    if (a.x >= a.y && a.x >= a.z) return direction.x > 0.0f ? 0 : 1;
    if (a.y >= a.z) return direction.y > 0.0f ? 2 : 3;
    return direction.z > 0.0f ? 4 : 5;
}

/// The six world-to-clip matrices of a point light's shadow: each a 90 degree view along one axis, depth 0 at `nearPlane`
/// and 1 at `farPlane` (the light's reach).
inline std::array<glm::mat4, 6> pointShadowFaces(const glm::vec3& lightPosition, float nearPlane, float farPlane) {
    const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, nearPlane, farPlane);
    std::array<glm::mat4, 6> faces;
    for (int f = 0; f < 6; f++) {
        const glm::vec3 direction = pointFaceDirection(f);
        const glm::vec3 up = f >= 4 ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        faces[f] = projection * glm::lookAtRH(lightPosition, lightPosition + direction, up);
    }
    return faces;
}

/// A shadow-map depth (0 near, 1 far) of a perspective view back to a distance along the view's axis.
inline float linearizePointDepth(float depth, float nearPlane, float farPlane) {
    return nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
}

} // namespace Shadows
} // namespace Propulsion
