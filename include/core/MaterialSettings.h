#pragma once

/**
 * MaterialSettings.h
 * How a material lets light through, beyond what the node graph says about colour: whether it is solid, cut out (leaves, fences),
 * or see-through (glass), and what the see-through kind does to light.
 *
 *  - Opaque: solid. The default, and what every material made before these settings was.
 *  - Masked: each point is either there or not. Where the alpha (the Opacity Mask texture, else the Base Color texture's alpha
 *    channel) is under the Cutoff, the surface isn't there at all: it casts no shadow and is seen through. For foliage.
 *  - Translucent: light passes through. Opacity (times the Opacity texture, if any) is how much is stopped: 1 is solid, 0 is
 *    invisible. Raster and Hybrid RT blend it over what is behind. The Path Tracer bends light through it: Index of Refraction
 *    is how much, the Base Color tints what passes, Roughness frosts it. For glass and water.
 *
 * It belongs to the Material asset (the .pmat file), after everything else, so older engines and older files still work.
 */

#include <cmath>

namespace Propulsion {

enum class BlendMode : int {
    Opaque = 0,
    Masked = 1,
    Translucent = 2,
};

constexpr int kBlendModeCount = 3;

inline const char* blendModeName(BlendMode mode) {
    switch (mode) {
        case BlendMode::Opaque: return "Opaque";
        case BlendMode::Masked: return "Masked";
        case BlendMode::Translucent: return "Translucent";
    }
    return "Opaque";
}

inline BlendMode blendModeFromInt(int value) {
    return (value >= 0 && value < kBlendModeCount) ? static_cast<BlendMode>(value) : BlendMode::Opaque;
}

struct MaterialSettings {
    BlendMode blendMode = BlendMode::Opaque;
    float opacity = 1.0f;             // Translucent: how much light is stopped (1 solid, 0 invisible)
    float maskCutoff = 0.5f;          // Masked: alpha under this is cut away
    bool twoSided = false;            // draw (and light) the back of surfaces too: leaves, glass sheets, cloth
    float ior = 1.5f;                 // Translucent: index of refraction (air 1, water 1.33, glass 1.5, diamond 2.4)
    bool thinWalled = false;          // Translucent: a sheet with no inside (a window pane, a leaf): light goes straight through, not bent
    float absorptionDistance = 1.0f;  // Translucent, thick: how far light travels inside before the Base Color is all that is left of it

    static constexpr float kMinIor = 1.0f;
    static constexpr float kMaxIor = 3.0f;
    static constexpr float kMinDistance = 0.001f;
    static constexpr float kMaxDistance = 1000.0f;

    /// Whether anything about this material needs more than an opaque draw.
    bool isOpaque() const { return blendMode == BlendMode::Opaque; }

    void sanitize() {
        blendMode = blendModeFromInt(static_cast<int>(blendMode));
        if (!(opacity >= 0.0f)) opacity = 0.0f;
        if (opacity > 1.0f) opacity = 1.0f;
        if (!(maskCutoff >= 0.0f)) maskCutoff = 0.0f;
        if (maskCutoff > 1.0f) maskCutoff = 1.0f;
        if (!(ior >= kMinIor)) ior = kMinIor;
        if (ior > kMaxIor) ior = kMaxIor;
        if (!(absorptionDistance >= kMinDistance)) absorptionDistance = kMinDistance;
        if (absorptionDistance > kMaxDistance) absorptionDistance = kMaxDistance;
    }

    bool operator==(const MaterialSettings& o) const {
        return blendMode == o.blendMode && opacity == o.opacity && maskCutoff == o.maskCutoff && twoSided == o.twoSided &&
               ior == o.ior && thinWalled == o.thinWalled && absorptionDistance == o.absorptionDistance;
    }
    bool operator!=(const MaterialSettings& o) const { return !(*this == o); }
};

} // namespace Propulsion
