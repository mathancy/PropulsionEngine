#pragma once

/**
 * SpringArm.h
 * What a spring arm does while Play runs, on top of where it sits in the editor (CameraSettings.h): the camera pulls in
 * when something solid is behind it, and the arm can lag behind the Object it is fixed to. Header-only, and it knows
 * nothing about the physics or the renderer (the caller says what is solid), so it is tested on its own
 * (tests/spring_arm_test.cpp).
 *
 * Like Unreal's SpringArm:
 *   collision  a ball (`probeSize`) is slid from the arm's fixed point to where the camera wants to be, socket offset
 *              included; if it meets something the camera stops there. It goes back out the moment the way is clear.
 *   lag        the arm's fixed point eases toward the Object's, and the arm's direction eases toward where the player is
 *              looking, each at its own speed. The lag is frame-rate independent: the gap closes by the same fraction in
 *              the same time however many frames that takes.
 */

#include "core/CameraSettings.h"

#include <cmath>
#include <functional>

namespace Propulsion {

/// The arm as it was last frame, which is what the lag eases from. Make one when Play starts.
struct SpringArmState {
    bool primed = false;  // has a frame been seen yet (the first one has nothing to ease from)
    ArmFrame frame;
};

/// If the arm's fixed point is further than this from where it should be it jumped there (a teleport), so it follows at
/// once instead of flying across the level.
inline constexpr float kArmTeleportDistance = 20.0f;

/// How much of the gap an easing closes in `deltaTime` seconds at `speed`. A speed of 0 (or less) means no easing at all.
inline float armLagBlend(float speed, float deltaTime) {
    if (!(speed > 0.0f)) return 1.0f;
    if (!(deltaTime > 0.0f)) return 0.0f;
    return 1.0f - std::exp(-speed * deltaTime);
}

/// Where the arm is this frame: `desired` (where the Object puts it), eased from where it was if lag is on. Remembers it.
inline ArmFrame laggedArmFrame(SpringArmState& state, const ArmFrame& desired, const SpringArmSettings& arm, float deltaTime) {
    ArmFrame frame = desired;
    const bool jumped = !state.primed || glm::distance(state.frame.origin, desired.origin) > kArmTeleportDistance;
    if (!jumped) {
        if (arm.enableCameraLag) {
            frame.origin = glm::mix(state.frame.origin, desired.origin, armLagBlend(arm.cameraLagSpeed, deltaTime));
            if (arm.cameraLagMaxDistance > 0.0f) {
                const glm::vec3 behind = frame.origin - desired.origin;
                const float distance = glm::length(behind);
                if (distance > arm.cameraLagMaxDistance) frame.origin = desired.origin + behind * (arm.cameraLagMaxDistance / distance);
            }
        }
        if (arm.enableCameraRotationLag) {
            frame.rotation = glm::normalize(glm::slerp(state.frame.rotation, desired.rotation, armLagBlend(arm.cameraRotationLagSpeed, deltaTime)));
        }
    }
    state.frame = frame;
    state.primed = true;
    return frame;
}

/// How much of the way from `from` to `to` a ball of `radius` can travel before it meets something solid: 1 if the way is
/// clear, 0 if it starts inside something.
using ArmProbe = std::function<float(const glm::vec3& from, const glm::vec3& to, float radius)>;

/// How much of the arm is clear (1 is all of it), from the arm's fixed point to where the camera wants to be.
inline float armReach(const ArmFrame& frame, const SpringArmSettings& arm, const ArmProbe& probe) {
    if (!arm.enabled || !arm.doCollisionTest || !probe) return 1.0f;
    const glm::vec3 reach = armReachVector(arm);
    if (glm::length(reach) < 1e-4f) return 1.0f;  // a first-person arm has nowhere to go
    const float clear = probe(frame.origin, frame.origin + frame.rotation * reach, arm.probeSize);
    return glm::clamp(clear, 0.0f, 1.0f);
}

/// Where the Play camera is this frame: the arm as the Object sets it, lagged, with the camera pulled in at what is solid.
/// `deltaTime` is how much game time has passed since the last frame.
inline glm::mat4 playCameraMatrix(SpringArmState& state, const glm::vec3& objectPosition, const glm::quat& objectRotation,
                                  const CameraSettings& cam, float lookPitchDegrees, float deltaTime, const ArmProbe& probe) {
    const ArmFrame desired = armFrame(objectPosition, objectRotation, cam.arm, lookPitchDegrees);
    if (!cam.arm.enabled) return cameraWorldMatrix(desired, cam);
    const ArmFrame frame = laggedArmFrame(state, desired, cam.arm, deltaTime);
    return cameraWorldMatrix(frame, cam, armReach(frame, cam.arm, probe));
}

} // namespace Propulsion
