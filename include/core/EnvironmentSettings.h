#pragma once

/**
 * EnvironmentSettings.h
 * The scene's environment: the sky that lights everything and is reflected in shiny surfaces.
 *
 * With no HDRI chosen the scene uses a plain sky gradient. An HDRI is a high dynamic range panorama (an
 * equirectangular .hdr file, such as the ones on Polyhaven). It lights the scene from every direction, shows up in
 * reflections, and can be drawn as the viewport background.
 *
 * It belongs to the project (the .pproj file), not to an Object.
 */

#include "core/RenderMode.h"

#include <string>

namespace Propulsion {

struct EnvironmentSettings {
    // The HDRI file, relative to the project folder with '/' separators ("Assets/HDRI/sunset.hdr"). Empty: the default sky.
    std::string hdriPath;
    float intensity = 1.0f;         // multiplies the whole environment's brightness
    float rotationDegrees = 0.0f;   // turns the environment about Z (up) like a positive rotation about Z: counter-clockwise seen from above
    bool showBackground = true;     // draw the sky behind the scene (otherwise a plain dark grey)

    // Shadows and ambient occlusion (how dark the sky's light gets in corners) in the normal render mode.
    bool shadows = true;
    float shadowDistance = 100.0f;    // how far from the camera shadows are drawn (world units); the nearer, the sharper they are
    bool ambientOcclusion = true;
    float aoRadius = 0.5f;            // how far from a surface things can darken it (world units)
    float aoStrength = 1.0f;          // 0 is none, 1 is full

    // How the viewport is drawn, and how many rays per pixel the ray-traced mode spends on each soft effect (more is smoother and slower).
    RenderMode renderMode = RenderMode::Raster;
    int rtShadowRays = 16;            // per light
    int rtAoRays = 16;

    static constexpr float kMaxIntensity = 1000.0f;
    static constexpr float kMinShadowDistance = 1.0f;
    static constexpr float kMaxShadowDistance = 5000.0f;
    static constexpr float kMinAoRadius = 0.05f;
    static constexpr float kMaxAoRadius = 10.0f;
    static constexpr int kMinRtRays = 1;
    static constexpr int kMaxRtRays = 64;

    void sanitize() {
        if (!(intensity >= 0.0f)) intensity = 0.0f;
        if (intensity > kMaxIntensity) intensity = kMaxIntensity;
        if (!(rotationDegrees > -100000.0f && rotationDegrees < 100000.0f)) rotationDegrees = 0.0f;
        if (!(shadowDistance >= kMinShadowDistance)) shadowDistance = kMinShadowDistance;
        if (shadowDistance > kMaxShadowDistance) shadowDistance = kMaxShadowDistance;
        if (!(aoRadius >= kMinAoRadius)) aoRadius = kMinAoRadius;
        if (aoRadius > kMaxAoRadius) aoRadius = kMaxAoRadius;
        if (!(aoStrength >= 0.0f)) aoStrength = 0.0f;
        if (aoStrength > 1.0f) aoStrength = 1.0f;
        renderMode = renderModeFromInt(static_cast<int>(renderMode));
        if (rtShadowRays < kMinRtRays) rtShadowRays = kMinRtRays;
        if (rtShadowRays > kMaxRtRays) rtShadowRays = kMaxRtRays;
        if (rtAoRays < kMinRtRays) rtAoRays = kMinRtRays;
        if (rtAoRays > kMaxRtRays) rtAoRays = kMaxRtRays;
    }

    bool operator==(const EnvironmentSettings& o) const {
        return hdriPath == o.hdriPath && intensity == o.intensity && rotationDegrees == o.rotationDegrees &&
               showBackground == o.showBackground && shadows == o.shadows && shadowDistance == o.shadowDistance &&
               ambientOcclusion == o.ambientOcclusion && aoRadius == o.aoRadius && aoStrength == o.aoStrength &&
               renderMode == o.renderMode && rtShadowRays == o.rtShadowRays && rtAoRays == o.rtAoRays;
    }
    bool operator!=(const EnvironmentSettings& o) const { return !(*this == o); }
};

} // namespace Propulsion
