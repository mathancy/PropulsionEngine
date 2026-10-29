#pragma once

/**
 * RenderMode.h
 * How the viewport is drawn.
 *
 *  - Raster: the normal real-time renderer (shadow maps, screen-space occlusion, environment reflections).
 *  - Hybrid RT: the same picture, but shadows, ambient occlusion and reflections come from rays traced through the scene.
 *    Needs a GPU with ray query support.
 *  - Path Tracer: the picture is built up from light paths bouncing around the scene, a little more with every frame the
 *    camera and scene stay still. Needs a GPU with ray query support.
 *
 * It belongs to the project (the .pproj file; see EnvironmentSettings).
 */

namespace Propulsion {

enum class RenderMode : int {
    Raster = 0,
    HybridRT = 1,
    PathTracer = 2,
};

constexpr int kRenderModeCount = 3;

inline const char* renderModeName(RenderMode mode) {
    switch (mode) {
        case RenderMode::Raster: return "Raster";
        case RenderMode::HybridRT: return "Hybrid RT";
        case RenderMode::PathTracer: return "Path Tracer";
    }
    return "Raster";
}

/// A mode from a number read from a file or a UI; anything that isn't a mode is Raster.
inline RenderMode renderModeFromInt(int value) {
    return (value >= 0 && value < kRenderModeCount) ? static_cast<RenderMode>(value) : RenderMode::Raster;
}

/// Whether a mode can be used on this GPU (`canTraceRays`: it can do ray queries).
inline bool renderModeAvailable(RenderMode mode, bool canTraceRays) {
    return mode == RenderMode::Raster || canTraceRays;
}

/// What the viewport really uses: the mode that was asked for, unless this GPU can't do it (then Raster, so a project made on
/// a ray tracing GPU still opens and draws on any other).
inline RenderMode effectiveRenderMode(RenderMode requested, bool canTraceRays) {
    return renderModeAvailable(requested, canTraceRays) ? requested : RenderMode::Raster;
}

} // namespace Propulsion
