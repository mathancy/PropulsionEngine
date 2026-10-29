#pragma once

#include <glm/glm.hpp>

namespace Propulsion {

enum class GizmoMode {
    None,
    Translate,
    Rotate,
    Scale
};

enum class GizmoAxis {
    None,
    X,
    Y,
    Z,
    XY,  // Plane handle
    XZ,  // Plane handle
    YZ,  // Plane handle
    Center  // Camera-relative movement
};

} // namespace Propulsion
