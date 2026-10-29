#pragma once

/**
 * LightingData.h
 * The lights and environment as the shader reads them, and the code that fills them in. No GPU calls here, so it is
 * tested without a window (tests/lighting_test.cpp). The layout matches `LightingData` in shaders/lighting.glsl
 * (std140: every member is a vec4 or a mat4, so there is no padding to get wrong).
 */

#include "core/LightSettings.h"
#include "rendering/EnvironmentMap.h"
#include "rendering/ShadowMath.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

namespace Propulsion {

constexpr int kMaxLights = 16;

/// One light in the world: its settings, where it is, and the way it shines.
struct SceneLight {
    LightSettings settings;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 1.0f, 0.0f};  // the way its light travels (directional lights)
    float specularScale = 1.0f;              // 0: it only lights diffuse surfaces (the soft fill of the default rig)
};

struct GpuLight {
    glm::vec4 positionType;  // directional: xyz = the direction TO the light, w = 0.  point: xyz = position, w = 1
    glm::vec4 colorRange;    // rgb = color * intensity, w = range (point lights)
    glm::vec4 params;        // x = specular scale, y = shadow slot (-1: none), z = the light's size for soft shadows
                             // (a point light's diameter; for the sun, how wide its disc is per unit of distance), w = reach (point lights)
};

constexpr int kPointShadowFaces = Shadows::kMaxPointShadows * Shadows::kFacesPerPoint;

struct GpuLighting {
    glm::vec4 envParams;  // x = environment intensity, y = environment yaw (radians), z = highest mip of the reflection image, w = light count
    glm::vec4 sh[9];      // the sky's diffuse lighting (rgb)
    GpuLight lights[kMaxLights];
    glm::mat4 dirShadowMatrix[Shadows::kCascadeCount];  // world to each of the sun's shadow maps (nearest the camera first)
    glm::vec4 dirShadowParams[Shadows::kCascadeCount];  // per map: x = 1 / map size, y = depth range (world units), z = map width (world units)
    glm::vec4 dirShadowSplits;   // how far from the camera each map reaches (along the view direction)
    glm::vec4 dirShadowGlobal;   // x = number of maps, y = 1 if the sun has a shadow, z = fraction of a map's depth it fades into the next over,
                                 // w = how far shadows are drawn
    glm::mat4 pointShadow[kPointShadowFaces];  // [slot * 6 + face]: world to that face of a point light's shadow
    glm::vec4 shadowGlobal;      // x = a point shadow face's size in texels, y = their near plane
    glm::vec4 rayParams;         // Hybrid RT: x = shadow rays per light, y = occlusion rays, z = occlusion radius, w = occlusion strength
};

static_assert(sizeof(GpuLight) == 48, "GpuLight must match the shader's std140 Light struct");
static_assert(offsetof(GpuLighting, lights) == 160, "the lights start after the SH coefficients");
static_assert(Shadows::kCascadeCount == 4, "dirShadowSplits holds one distance per map: a vec4");
static_assert(offsetof(GpuLighting, dirShadowMatrix) == 160 + kMaxLights * 48, "the sun's shadow follows the lights");
static_assert(sizeof(GpuLighting) == 160 + kMaxLights * 48 + Shadows::kCascadeCount * 64 + Shadows::kCascadeCount * 16 + 16 + 16 + kPointShadowFaces * 64 + 16 + 16,
              "GpuLighting must match the shader's std140 block");

/// The size a light has for soft shadows: a point light's diameter, and for the sun the width its disc spreads per unit of
/// distance (a blocker this far from the ground gives a penumbra this many times wider).
inline float shadowLightSize(const LightSettings& s) {
    if (s.type == LightSettings::Type::Directional) {
        return 2.0f * std::tan(glm::radians(s.angularDiameterDeg) * 0.5f);
    }
    return 2.0f * s.radius;
}

inline GpuLight packLight(const SceneLight& light) {
    const LightSettings& s = light.settings;
    GpuLight out;
    if (s.type == LightSettings::Type::Directional) {
        out.positionType = glm::vec4(-glm::normalize(light.direction), 0.0f);
        out.colorRange = glm::vec4(s.color * s.intensity, 0.0f);
    } else {
        out.positionType = glm::vec4(light.position, 1.0f);
        out.colorRange = glm::vec4(s.color * s.intensity, s.range);
    }
    out.params = glm::vec4(light.specularScale, -1.0f, shadowLightSize(s), s.range);
    return out;
}

/// The lights the scene had before Object lights existed: a warm key light from above and to one side, and a soft
/// cool fill that only lights diffuse surfaces. A scene with no lights of its own is still lit by these. (Only the key light
/// casts a shadow.)
inline std::vector<SceneLight> defaultLightRig() {
    std::vector<SceneLight> rig(2);

    rig[0].settings = LightSettings::directional();
    rig[0].direction = -glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));

    rig[1].settings = LightSettings::directional();
    rig[1].settings.color = glm::vec3(0.4f, 0.5f, 0.6f);
    rig[1].settings.intensity = 0.5f;
    rig[1].settings.castShadows = false;
    rig[1].direction = -glm::normalize(glm::vec3(-0.3f, 0.5f, -0.5f));
    rig[1].specularScale = 0.0f;
    return rig;
}

struct EnvironmentLighting {
    float intensity = 1.0f;
    float yawRadians = 0.0f;
    float maxLod = 0.0f;
    Environment::Sh9 sh;
};

/// Which of the packed lights have a shadow map, and what the shadow renderer needs to know about them. Indices are
/// into `lights` (the lights as packed, in the same order as the GpuLighting's).
struct ShadowAssignment {
    std::vector<SceneLight> lights;
    int directional = -1;                                              // the one sun that casts a shadow, or -1
    std::array<int, Shadows::kMaxPointShadows> point{-1, -1, -1, -1};  // [slot]: the point light that has it, or -1
    int pointCount = 0;
};

/// Fill in what the shader reads. Lights that are off, or contribute nothing, are skipped; beyond kMaxLights the
/// extra ones are ignored (`dropped`, if given, says how many). With `useDefaultRigIfEmpty` a scene with no light
/// objects at all gets the default rig. If `shadows` is given, lights that cast shadows are given shadow slots (the first
/// sun, and the first few point lights) and `shadows` says which; their matrices are filled in by `applyShadowFrame`.
/// With `rayTracedShadows` every light that casts shadows is marked as one (a ray is traced to it: no map, no limit on how
/// many) and `shadows` is left empty.
inline GpuLighting buildLighting(const std::vector<SceneLight>& lights, const EnvironmentLighting& env, bool useDefaultRigIfEmpty,
                                 int* dropped = nullptr, ShadowAssignment* shadows = nullptr, bool rayTracedShadows = false) {
    GpuLighting out{};
    out.envParams = glm::vec4(env.intensity, env.yawRadians, env.maxLod, 0.0f);
    for (int i = 0; i < 9; i++) out.sh[i] = glm::vec4(env.sh.c[i], 0.0f);
    for (int i = 0; i < kMaxLights; i++) out.lights[i].params = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    if (shadows) *shadows = ShadowAssignment();

    int count = 0;
    int extra = 0;
    auto add = [&](const SceneLight& light) {
        if (!light.settings.enabled || light.settings.intensity <= 0.0f) return;
        if (count >= kMaxLights) {
            extra++;
            return;
        }
        out.lights[count] = packLight(light);
        if (rayTracedShadows) {
            if (light.settings.castShadows) out.lights[count].params.y = 0.0f;
        } else if (shadows) {
            shadows->lights.push_back(light);
            if (light.settings.castShadows) {
                if (light.settings.type == LightSettings::Type::Directional) {
                    if (shadows->directional < 0) {
                        shadows->directional = count;
                        out.lights[count].params.y = 0.0f;
                    }
                } else if (shadows->pointCount < Shadows::kMaxPointShadows) {
                    const int slot = shadows->pointCount++;
                    shadows->point[slot] = count;
                    out.lights[count].params.y = static_cast<float>(slot);
                }
            }
        }
        count++;
    };
    bool anyLightObject = false;
    for (const SceneLight& light : lights) {
        anyLightObject = anyLightObject || light.settings.enabled;
        add(light);
    }
    // (A light turned down to nothing is still the scene's light: the rig is only for scenes with none at all.)
    if (!anyLightObject && useDefaultRigIfEmpty) {
        for (const SceneLight& light : defaultLightRig()) add(light);
    }

    out.envParams.w = static_cast<float>(count);
    if (dropped) *dropped = extra;
    return out;
}

/// What Hybrid RT needs besides the lights: how many rays each soft effect spends per pixel, and the occlusion's reach.
inline void applyRayTracedSettings(GpuLighting& gpu, int shadowRays, int occlusionRays, float occlusionRadius, float occlusionStrength) {
    gpu.rayParams = glm::vec4(static_cast<float>(shadowRays), static_cast<float>(occlusionRays), occlusionRadius, occlusionStrength);
}

/// What the shadow renderer draws this frame: the sun's map, and the point lights' faces.
struct ShadowFrame {
    Shadows::CascadedShadow directional;     // directional.valid: the sun's maps are drawn and used
    int pointCount = 0;
    std::array<glm::vec3, Shadows::kMaxPointShadows> pointPosition{};
    std::array<float, Shadows::kMaxPointShadows> pointReach{};
    std::array<std::array<glm::mat4, 6>, Shadows::kMaxPointShadows> pointFaces{};
};

/// Work out the shadow matrices for the lights that were given slots, and write them into the lighting block. The sun's maps
/// cover the camera's view (`view`, `projection`) from `nearDistance` out to `shadowDistance`. A light that can't have a
/// shadow (the sun's slot with nothing to fit) gets its slot taken away again.
inline ShadowFrame applyShadowFrame(GpuLighting& gpu, const ShadowAssignment& assignment, const glm::mat4& view, const glm::mat4& projection,
                                    float nearDistance, float shadowDistance, const Shadows::Aabb& casters) {
    ShadowFrame frame;
    for (int c = 0; c < Shadows::kCascadeCount; c++) {
        gpu.dirShadowMatrix[c] = glm::mat4(1.0f);
        gpu.dirShadowParams[c] = glm::vec4(1.0f / Shadows::kCascadeMapSize, 1.0f, 1.0f, 0.0f);
    }
    gpu.dirShadowSplits = glm::vec4(0.0f);
    gpu.dirShadowGlobal = glm::vec4(0.0f);
    for (glm::mat4& m : gpu.pointShadow) m = glm::mat4(1.0f);
    gpu.shadowGlobal = glm::vec4(static_cast<float>(Shadows::kPointMapSize), Shadows::kPointNearPlane, 0.0f, 0.0f);

    if (assignment.directional >= 0) {
        const SceneLight& sun = assignment.lights[assignment.directional];
        frame.directional = Shadows::fitCascadedShadow(view, projection, nearDistance, shadowDistance, sun.direction, casters, Shadows::kCascadeMapSize);
        if (frame.directional.valid) {
            for (int c = 0; c < Shadows::kCascadeCount; c++) {
                const Shadows::DirectionalShadow& map = frame.directional.maps[c];
                gpu.dirShadowMatrix[c] = map.viewProjection;
                gpu.dirShadowParams[c] = glm::vec4(1.0f / Shadows::kCascadeMapSize, map.depthRange, map.orthoWidth, 0.0f);
            }
            gpu.dirShadowSplits = glm::vec4(frame.directional.splitDepth[0], frame.directional.splitDepth[1], frame.directional.splitDepth[2],
                                            frame.directional.splitDepth[3]);
            gpu.dirShadowGlobal = glm::vec4(static_cast<float>(frame.directional.count), 1.0f, Shadows::kCascadeSplitBlend,
                                            frame.directional.splitDepth[Shadows::kCascadeCount - 1]);
        } else {
            gpu.lights[assignment.directional].params.y = -1.0f;
        }
    }

    for (int slot = 0; slot < assignment.pointCount; slot++) {
        const SceneLight& bulb = assignment.lights[assignment.point[slot]];
        const float reach = (std::max)(bulb.settings.range, Shadows::kPointNearPlane * 2.0f);
        frame.pointPosition[slot] = bulb.position;
        frame.pointReach[slot] = reach;
        frame.pointFaces[slot] = Shadows::pointShadowFaces(bulb.position, Shadows::kPointNearPlane, reach);
        for (int f = 0; f < 6; f++) gpu.pointShadow[slot * Shadows::kFacesPerPoint + f] = frame.pointFaces[slot][f];
    }
    frame.pointCount = assignment.pointCount;
    return frame;
}

} // namespace Propulsion
