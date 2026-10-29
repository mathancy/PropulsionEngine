#pragma once

/**
 * CameraSettings.h
 * The Camera component an Object asset can carry, modeled on Unreal's CameraComponent.
 *
 * An Object with a camera is a view into the world. While Play runs, the one that auto-activates
 * for the player is what the viewport shows. The camera sits at an offset from the Object, so a
 * third-person camera is "behind and above" and a first-person one is at eye height.
 *
 * Conventions (the engine's world): Z is up, forward is +Y, right is +X (right-handed, Blender-style).
 * Rotation is Euler degrees in the same order as Transform.Rot: pitch about X, roll about Y (the way
 * the camera looks) and yaw about Z. A camera with no rotation looks along +Y with Z up.
 */

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Propulsion {

/// Unreal's SpringArm: a boom the camera sits at the end of, so a third-person camera orbits the Object instead of being
/// fixed to it. The arm is fixed to the Object at `targetOffset` and points forward; the camera hangs `targetArmLength`
/// behind that point, looking along the arm, so it looks at the point the arm is fixed to. A length of 0 is a
/// first-person camera: it sits at the target offset (eye height) and turns on the spot.
///
/// With `usePawnControlRotation` the Object's pitch (its rotation about X, which the script's look up/down changes)
/// swings only the arm while Play runs. The Object stays upright, so it walks along the ground and its mesh doesn't
/// tilt. The Object's yaw turns the whole arm with it.
struct SpringArmSettings {
    bool enabled = false;               // does the Object have a spring arm component at all
    float targetArmLength = 4.0f;       // how far behind its fixed point the camera hangs (world units)
    glm::vec3 targetOffset{0.0f};       // where the arm is fixed, relative to the Object (head height, say)
    glm::vec3 socketOffset{0.0f};       // the camera's offset from the arm's end, in the arm's own directions (an over-the-shoulder view)
    bool usePawnControlRotation = true; // looking up and down swings the arm (and not the Object)

    // While Play runs (core/SpringArm.h): the camera pulls in when something solid is behind it, and can trail behind.
    bool doCollisionTest = true;        // the camera stops in front of solid objects instead of going through them
    float probeSize = 0.2f;             // the radius of the ball slid along the arm to find them (keep it under the character's radius)
    bool enableCameraLag = false;       // the arm's fixed point trails behind the Object when it moves
    float cameraLagSpeed = 10.0f;       // how quickly it catches up (higher is tighter; 0 means no lag at all)
    float cameraLagMaxDistance = 0.0f;  // the most it may trail behind (0 for no limit)
    bool enableCameraRotationLag = false;  // the arm turns after where the player is looking, instead of with it
    float cameraRotationLagSpeed = 10.0f;  // how quickly it catches up (0 means no lag at all)

    static constexpr float kMaxLength = 10000.0f;

    void sanitize() {
        if (!(targetArmLength >= 0.0f)) targetArmLength = 0.0f;
        if (targetArmLength > kMaxLength) targetArmLength = kMaxLength;
        if (!(probeSize >= 0.0f)) probeSize = 0.0f;
        if (probeSize > kMaxLength) probeSize = kMaxLength;
        if (!(cameraLagSpeed >= 0.0f)) cameraLagSpeed = 0.0f;
        if (!(cameraRotationLagSpeed >= 0.0f)) cameraRotationLagSpeed = 0.0f;
        if (!(cameraLagMaxDistance >= 0.0f)) cameraLagMaxDistance = 0.0f;
    }

    bool operator==(const SpringArmSettings& o) const {
        return enabled == o.enabled && targetArmLength == o.targetArmLength && targetOffset == o.targetOffset &&
               socketOffset == o.socketOffset && usePawnControlRotation == o.usePawnControlRotation &&
               doCollisionTest == o.doCollisionTest && probeSize == o.probeSize && enableCameraLag == o.enableCameraLag &&
               cameraLagSpeed == o.cameraLagSpeed && cameraLagMaxDistance == o.cameraLagMaxDistance &&
               enableCameraRotationLag == o.enableCameraRotationLag && cameraRotationLagSpeed == o.cameraRotationLagSpeed;
    }
    bool operator!=(const SpringArmSettings& o) const { return !(*this == o); }
};

struct CameraSettings {
    bool enabled = false;          // does the Object have a camera component at all
    float fov = 90.0f;             // horizontal field of view in degrees (Unreal's default)
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    bool autoActivate = true;      // "Auto Activate for Player": Play looks through this camera
    glm::vec3 offset{0.0f};        // location relative to the Object, or to the end of the spring arm when it has one
    glm::vec3 rotation{0.0f};      // rotation relative to the same (pitch about X, roll about Y, yaw about Z)
    SpringArmSettings arm;         // a separate component in the editor; stored here because only the camera uses it

    /// camera.fbx was modeled looking down -Z with Y up (how Blender's FBX exporter writes its default axes).
    /// This turns it a quarter about X so it looks along +Y with Z up, the way the world's cameras do.
    /// (If the model is ever exported already in the world's axes, this becomes the identity.)
    static glm::mat4 meshCorrection() {
        return glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    static constexpr float kMinFov = 5.0f;
    static constexpr float kMaxFov = 170.0f;

    /// Pull every value back into a range that makes a valid projection.
    void sanitize() {
        if (!(fov >= kMinFov)) fov = kMinFov;
        if (fov > kMaxFov) fov = kMaxFov;
        if (!(nearClip >= 0.001f)) nearClip = 0.001f;
        if (!(farClip > nearClip + 0.001f)) farClip = nearClip + 0.001f;
        arm.sanitize();
    }

    bool operator==(const CameraSettings& o) const {
        return enabled == o.enabled && fov == o.fov && nearClip == o.nearClip && farClip == o.farClip &&
               autoActivate == o.autoActivate && offset == o.offset && rotation == o.rotation && arm == o.arm;
    }
    bool operator!=(const CameraSettings& o) const { return !(*this == o); }
};

/// Where a spring arm is fixed in the world, and which way it points.
struct ArmFrame {
    glm::vec3 origin{0.0f};                       // the point the arm is fixed at, in the world
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};   // which way it points (the camera looks along its +Y), in the world
};

/// Where the arm is fixed and which way it points: at the target offset, swung by `lookPitchDegrees`. Without an arm
/// that is the Object itself.
inline ArmFrame armFrame(const glm::vec3& objectPosition, const glm::quat& objectRotation, const SpringArmSettings& arm,
                         float lookPitchDegrees = 0.0f) {
    ArmFrame frame;
    frame.origin = objectPosition;
    frame.rotation = objectRotation;
    if (arm.enabled) {
        frame.origin += objectRotation * arm.targetOffset;
        frame.rotation = objectRotation * glm::angleAxis(glm::radians(lookPitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    return frame;
}

/// From the arm's fixed point to the camera's place at the end of it, in the arm's own directions: back along the arm,
/// then the socket offset.
inline glm::vec3 armReachVector(const SpringArmSettings& arm) {
    return glm::vec3(0.0f, -arm.targetArmLength, 0.0f) + arm.socketOffset;
}

/// The camera in the world for an arm in `frame`. `reach` is how much of the arm is clear (1 is all of it): a wall behind
/// the camera pulls it in along the arm.
inline glm::mat4 cameraWorldMatrix(const ArmFrame& frame, const CameraSettings& cam, float reach = 1.0f) {
    glm::mat4 world = glm::translate(glm::mat4(1.0f), frame.origin) * glm::mat4_cast(frame.rotation);
    if (cam.arm.enabled) world = world * glm::translate(glm::mat4(1.0f), armReachVector(cam.arm) * reach);
    return world * glm::translate(glm::mat4(1.0f), cam.offset) * glm::mat4_cast(glm::quat(glm::radians(cam.rotation)));
}

/// Where a camera is in the world: the owning Object's position and rotation (not its scale, so a stretched object
/// doesn't stretch its camera), then the spring arm if there is one, then the camera's own offset and rotation. The
/// camera looks along the result's +Y with +Z up.
///
/// The arm is fixed at its target offset, swung by `lookPitchDegrees` (the Object's pitch, moved onto the arm; see
/// SpringArmSettings), and the camera hangs the arm's length behind that point plus the socket offset. Tilting the
/// Object itself by the same pitch gives the same result when the offset is zero and the arm has no length.
///
/// This is the arm as the editor shows it, at full length. While Play runs the camera also pulls in at walls and may lag
/// (core/SpringArm.h), through the same pieces.
inline glm::mat4 cameraWorldMatrix(const glm::vec3& objectPosition, const glm::quat& objectRotation, const CameraSettings& cam,
                                   float lookPitchDegrees = 0.0f) {
    return cameraWorldMatrix(armFrame(objectPosition, objectRotation, cam.arm, lookPitchDegrees), cam);
}

/// The projection wants the vertical field of view; a camera component stores the horizontal one (as Unreal does).
/// `aspect` is width over height. A 90 degree horizontal view on a 16:9 screen is about 58.7 degrees tall.
inline float verticalFovFromHorizontal(float horizontalDegrees, float aspect) {
    if (!(aspect > 0.0f)) aspect = 1.0f;
    return glm::degrees(2.0f * std::atan(std::tan(glm::radians(horizontalDegrees) * 0.5f) / aspect));
}

} // namespace Propulsion
