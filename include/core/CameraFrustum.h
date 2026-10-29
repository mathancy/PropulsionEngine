#pragma once

/**
 * CameraFrustum.h
 * The outline drawn for a selected Camera component: the pyramid of what the camera sees.
 *
 * Everything here is in the camera's own space, where the camera sits at the origin looking along +Y
 * with +Z up and +X to the right (the engine's world convention). The editor moves the points into the
 * world and onto the screen. Kept free of Vulkan and ImGui so it can be tested on its own.
 */

#include "core/CameraSettings.h"

#include <cmath>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace Propulsion {

using Segment = std::pair<glm::vec3, glm::vec3>;

/// How far from the camera the outline reaches. The far clip can be a thousand units, which would
/// make the outline fill the view, so it stops short.
inline float frustumDisplayDepth(const CameraSettings& cam) {
    return cam.farClip < 3.0f ? cam.farClip : 3.0f;
}

/// Half the width of the view at `depth` in front of the camera. `fov` is the horizontal field of view.
inline float frustumHalfWidth(float fovDegrees, float depth) {
    return depth * std::tan(glm::radians(fovDegrees) * 0.5f);
}

/// The outline as line segments: four edges from the camera to the far rectangle, the far rectangle,
/// the near rectangle (only when the near clip is far enough out to see), and a small triangle above
/// the top edge showing which way is up (as Unreal does). `aspect` is width over height.
inline std::vector<Segment> frustumSegments(const CameraSettings& cam, float aspect) {
    std::vector<Segment> out;
    const float depth = frustumDisplayDepth(cam);
    if (!(aspect > 0.0f) || !(depth > 0.0f)) return out;

    auto rectangle = [&](float z, std::vector<glm::vec3>& corners) {
        const float w = frustumHalfWidth(cam.fov, z);
        const float h = w / aspect;
        corners = {{-w, z, h}, {w, z, h}, {w, z, -h}, {-w, z, -h}};  // top-left, top-right, bottom-right, bottom-left
    };

    std::vector<glm::vec3> far;
    rectangle(depth, far);
    for (int i = 0; i < 4; ++i) {
        out.push_back({glm::vec3(0.0f), far[i]});
        out.push_back({far[i], far[(i + 1) % 4]});
    }

    if (cam.nearClip > 0.2f && cam.nearClip < depth) {
        std::vector<glm::vec3> nearRect;
        rectangle(cam.nearClip, nearRect);
        for (int i = 0; i < 4; ++i) out.push_back({nearRect[i], nearRect[(i + 1) % 4]});
    }

    // The "up" marker: a triangle sitting on the far rectangle's top edge.
    const float w = frustumHalfWidth(cam.fov, depth);
    const float h = w / aspect;
    const glm::vec3 left(-w * 0.3f, depth, h), right(w * 0.3f, depth, h), tip(0.0f, depth, h * 1.4f);
    out.push_back({left, tip});
    out.push_back({tip, right});
    out.push_back({right, left});
    return out;
}

/// Trim a segment to the part in front of the viewing camera, at least `nearDistance` away. The points are in
/// the viewing camera's *view space* (what glm::lookAt produces: it looks down -Z whatever the world's axes).
/// Returns false if none of the segment is in front. Needed because a point behind the camera can't be
/// projected to the screen.
inline bool clipSegmentToView(glm::vec3& a, glm::vec3& b, float nearDistance = 0.01f) {
    const float za = -a.z - nearDistance;  // positive when in front
    const float zb = -b.z - nearDistance;
    if (za < 0.0f && zb < 0.0f) return false;
    if (za >= 0.0f && zb >= 0.0f) return true;
    const float t = za / (za - zb);  // where the segment crosses the plane
    const glm::vec3 cut = a + (b - a) * t;
    if (za < 0.0f) a = cut;
    else b = cut;
    return true;
}

} // namespace Propulsion
