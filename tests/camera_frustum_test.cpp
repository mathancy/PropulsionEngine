// Tests for the camera frustum outline math (include/core/CameraFrustum.h).
//
//   cmake --build . --target CameraFrustumTest --config Debug
//   build/tests/CameraFrustumTest.exe

#include "core/Camera.h"
#include "core/CameraFrustum.h"

#include <cmath>
#include <limits>
#include <cstdio>

using namespace Propulsion;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

static void testHalfWidth() {
    CHECK(near(frustumHalfWidth(90.0f, 2.0f), 2.0f));            // 90 degrees: as wide as it is deep
    CHECK(near(frustumHalfWidth(60.0f, 1.0f), std::tan(glm::radians(30.0f))));
    CHECK(frustumHalfWidth(120.0f, 1.0f) > frustumHalfWidth(60.0f, 1.0f));
}

static void testDisplayDepth() {
    CameraSettings cam;
    CHECK(near(frustumDisplayDepth(cam), 3.0f));  // default far clip is 1000: stop short
    cam.farClip = 1.5f;
    CHECK(near(frustumDisplayDepth(cam), 1.5f));  // a short far clip wins
}

static void testShape() {
    CameraSettings cam;  // fov 90, near 0.1, far 1000
    const float aspect = 2.0f;
    auto segs = frustumSegments(cam, aspect);
    // 4 edges + 4 far-rectangle sides + 3 up-marker sides; no near rectangle at the default near clip.
    CHECK(segs.size() == 11);

    // The camera looks along +Y with Z up: every segment stays in front of it (y >= 0), and nothing is NaN.
    for (const Segment& s : segs) {
        CHECK(std::isfinite(s.first.x) && std::isfinite(s.second.z));
        CHECK(s.first.y >= 0.0f && s.second.y >= 0.0f);
    }

    // The far rectangle for fov 90, depth 3: half width 3 (x), half height 1.5 (z, aspect 2), at y = 3.
    bool foundTopRight = false;
    for (const Segment& s : segs) {
        if (near(s.second.x, 3.0f) && near(s.second.y, 3.0f) && near(s.second.z, 1.5f)) foundTopRight = true;
    }

    // Up is +Z: the marker triangle sits above the top edge, the highest point of the outline.
    float highest = 0.0f;
    for (const Segment& s : segs) highest = std::fmax(highest, std::fmax(s.first.z, s.second.z));
    CHECK(near(highest, 1.5f * 1.4f));
    CHECK(foundTopRight);

    // The first four segments leave the camera.
    CHECK(segs[0].first == glm::vec3(0.0f));
}

static void testNearRectangle() {
    CameraSettings cam;
    cam.nearClip = 1.0f;
    CHECK(frustumSegments(cam, 1.0f).size() == 15);  // the near rectangle appears
    cam.nearClip = 5.0f;                              // beyond the display depth: nothing to draw
    CHECK(frustumSegments(cam, 1.0f).size() == 11);
}

static void testBadInput() {
    CameraSettings cam;
    CHECK(frustumSegments(cam, 0.0f).empty());
    CHECK(frustumSegments(cam, -1.0f).empty());
    CHECK(frustumSegments(cam, std::nanf("")).empty());
}

static void testClip() {
    // Entirely in front: untouched.
    glm::vec3 a(0, 0, -1), b(1, 0, -3);
    CHECK(clipSegmentToView(a, b));
    CHECK(a == glm::vec3(0, 0, -1) && b == glm::vec3(1, 0, -3));

    // Entirely behind the camera: dropped.
    a = {0, 0, 1};
    b = {1, 0, 2};
    CHECK(!clipSegmentToView(a, b));

    // Crossing: the behind end is pulled up to the plane, the front end is kept.
    a = {0, 0, 2};     // behind
    b = {0, 0, -2};    // in front
    CHECK(clipSegmentToView(a, b, 0.01f));
    CHECK(near(a.z, -0.01f));
    CHECK(b == glm::vec3(0, 0, -2));

    // Same, with the ends the other way round.
    a = {0, 0, -2};
    b = {0, 0, 2};
    CHECK(clipSegmentToView(a, b, 0.01f));
    CHECK(near(b.z, -0.01f));
}

// ---- where a camera is, and how Play points the viewport camera at it ----

static bool nearVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }

static void testVerticalFov() {
    CHECK(near(verticalFovFromHorizontal(90.0f, 1.0f), 90.0f));                   // square: same both ways
    CHECK(near(verticalFovFromHorizontal(90.0f, 16.0f / 9.0f), 58.7155f, 0.01f));  // 90 wide on a 16:9 screen
    // The two fields of view describe the same pyramid: tan(h/2) = aspect * tan(v/2).
    const float v = verticalFovFromHorizontal(100.0f, 2.0f);
    CHECK(near(std::tan(glm::radians(100.0f) * 0.5f), 2.0f * std::tan(glm::radians(v) * 0.5f), 1e-3f));
    CHECK(near(verticalFovFromHorizontal(90.0f, 0.0f), 90.0f));  // a bad aspect doesn't produce NaN
}

static void testCameraWorldMatrix() {
    CameraSettings cam;  // no offset, no rotation of its own
    glm::mat4 world = cameraWorldMatrix(glm::vec3(1, 2, 3), glm::quat(glm::vec3(0)), cam);
    CHECK(nearVec(glm::vec3(world[3]), glm::vec3(1, 2, 3)));
    CHECK(nearVec(glm::vec3(world * glm::vec4(0, 1, 0, 0)), glm::vec3(0, 1, 0)));  // looks along +Y
    CHECK(nearVec(glm::vec3(world * glm::vec4(0, 0, 1, 0)), glm::vec3(0, 0, 1)));  // Z is up

    // A yaw of 90 degrees (about Z) turns the object, and its camera, to face -X.
    const glm::quat yaw90(glm::radians(glm::vec3(0, 0, 90)));
    world = cameraWorldMatrix(glm::vec3(1, 2, 3), yaw90, cam);
    CHECK(nearVec(glm::vec3(world * glm::vec4(0, 1, 0, 0)), glm::vec3(-1, 0, 0)));

    // A third-person camera: 3 behind and 2 above. Behind is -Y for an object facing +Y, and the offset turns with the object.
    cam.offset = glm::vec3(0, -3, 2);
    world = cameraWorldMatrix(glm::vec3(1, 2, 3), glm::quat(glm::vec3(0)), cam);
    CHECK(nearVec(glm::vec3(world[3]), glm::vec3(1, -1, 5)));
    world = cameraWorldMatrix(glm::vec3(1, 2, 3), yaw90, cam);
    CHECK(nearVec(glm::vec3(world[3]), glm::vec3(4, 2, 5)));  // facing -X, so behind it is +X

    // The camera's own rotation: pitched down 90 degrees (about X) it looks straight down at the ground.
    cam = CameraSettings();
    cam.rotation = glm::vec3(-90, 0, 0);
    world = cameraWorldMatrix(glm::vec3(0), glm::quat(glm::vec3(0)), cam);
    CHECK(nearVec(glm::vec3(world * glm::vec4(0, 1, 0, 0)), glm::vec3(0, 0, -1)));
}

// A spring arm holds the camera behind a fixed point; the player's look pitch swings the arm, and the Object itself is
// turned by yaw alone.
static void testSpringArm() {
    const glm::quat noTurn(glm::vec3(0));
    const glm::quat yaw90(glm::radians(glm::vec3(0, 0, 90)));
    auto forwardOf = [](const glm::mat4& world) { return glm::vec3(world * glm::vec4(0, 1, 0, 0)); };

    // First person: an arm of no length at eye height, so looking up turns the camera on the spot.
    CameraSettings eye;
    eye.arm.enabled = true;
    eye.arm.targetArmLength = 0.0f;
    eye.arm.targetOffset = glm::vec3(0, 0, 1.7f);
    glm::mat4 world = cameraWorldMatrix(glm::vec3(1, 2, 3), yaw90, eye, 90.0f);
    CHECK(nearVec(glm::vec3(world[3]), glm::vec3(1, 2, 4.7f)));        // still at the eye
    CHECK(nearVec(forwardOf(world), glm::vec3(0, 0, 1)));              // looking straight up
    world = cameraWorldMatrix(glm::vec3(0), yaw90, eye, 30.0f);
    CHECK(nearVec(forwardOf(world), glm::vec3(-std::cos(glm::radians(30.0f)), 0, std::sin(glm::radians(30.0f)))));  // 30 up, facing -X

    // Third person: the camera hangs 5 behind the Object. Looking up swings it down and under, looking down up and over.
    CameraSettings behind;
    behind.arm.enabled = true;
    behind.arm.targetArmLength = 5.0f;
    world = cameraWorldMatrix(glm::vec3(1, 2, 3), noTurn, behind, 0.0f);
    CHECK(nearVec(glm::vec3(world[3]), glm::vec3(1, -3, 3)));          // level: straight behind
    CHECK(nearVec(forwardOf(world), glm::vec3(0, 1, 0)));
    world = cameraWorldMatrix(glm::vec3(1, 2, 3), noTurn, behind, 90.0f);
    CHECK(nearVec(glm::vec3(world[3]), glm::vec3(1, 2, -2)));          // looking up: 5 below
    CHECK(nearVec(forwardOf(world), glm::vec3(0, 0, 1)));              // and looking up at it
    world = cameraWorldMatrix(glm::vec3(0), noTurn, behind, -90.0f);
    CHECK(nearVec(glm::vec3(world[3]), glm::vec3(0, 0, 5)));           // looking down: 5 above
    CHECK(nearVec(forwardOf(world), glm::vec3(0, 0, -1)));
    world = cameraWorldMatrix(glm::vec3(0), yaw90, behind, 0.0f);
    CHECK(nearVec(glm::vec3(world[3]), glm::vec3(5, 0, 0)));           // facing -X, so behind it is +X

    // Fixed above the Object's origin (its head), the camera orbits that point: always 5 away, always looking at it.
    behind.arm.targetOffset = glm::vec3(0, 0, 1.5f);
    for (float pitch : {-60.0f, -20.0f, 0.0f, 15.0f, 45.0f, 80.0f}) {
        world = cameraWorldMatrix(glm::vec3(0), yaw90, behind, pitch);
        const glm::vec3 head(0, 0, 1.5f);
        CHECK(near(glm::length(glm::vec3(world[3]) - head), 5.0f));
        CHECK(nearVec(glm::normalize(head - glm::vec3(world[3])), forwardOf(world)));
    }
    behind.arm.targetArmLength = 4.0f;
    world = cameraWorldMatrix(glm::vec3(0), noTurn, behind, 90.0f);
    CHECK(nearVec(glm::vec3(world[3]), glm::vec3(0, 0, -2.5f)));       // 4 below the head

    // The socket offset slides the camera sideways from the arm's end, turning with the arm (over the shoulder).
    CameraSettings shoulder;
    shoulder.arm.enabled = true;
    shoulder.arm.targetArmLength = 5.0f;
    shoulder.arm.socketOffset = glm::vec3(1, 0, 0);
    CHECK(nearVec(glm::vec3(cameraWorldMatrix(glm::vec3(0), noTurn, shoulder)[3]), glm::vec3(1, -5, 0)));
    CHECK(nearVec(glm::vec3(cameraWorldMatrix(glm::vec3(0), yaw90, shoulder)[3]), glm::vec3(5, 1, 0)));

    // The camera's own offset and rotation come after the arm.
    shoulder.arm.socketOffset = glm::vec3(0.0f);
    shoulder.offset = glm::vec3(0, 0, 2);
    CHECK(nearVec(glm::vec3(cameraWorldMatrix(glm::vec3(0), noTurn, shoulder)[3]), glm::vec3(0, -5, 2)));
    shoulder.rotation = glm::vec3(-90, 0, 0);
    CHECK(nearVec(forwardOf(cameraWorldMatrix(glm::vec3(0), noTurn, shoulder)), glm::vec3(0, 0, -1)));

    // Without an arm the camera is where it always was, and the look pitch does nothing to it.
    CameraSettings plain;
    plain.offset = glm::vec3(0, -3, 2);
    plain.arm.targetArmLength = 5.0f;  // set, but the component isn't there
    CHECK(nearVec(glm::vec3(cameraWorldMatrix(glm::vec3(0), yaw90, plain, 45.0f)[3]), glm::vec3(3, 0, 2)));
    CHECK(nearVec(forwardOf(cameraWorldMatrix(glm::vec3(0), yaw90, plain, 45.0f)), glm::vec3(-1, 0, 0)));

    // With no length and no offsets, pitching the arm is pitching the Object, which is what the arm replaces.
    CameraSettings atTheMiddle;
    atTheMiddle.arm.enabled = true;
    atTheMiddle.arm.targetArmLength = 0.0f;
    const glm::quat pitched(glm::radians(glm::vec3(30, 0, 90)));
    const glm::mat4 viaObject = cameraWorldMatrix(glm::vec3(0), pitched, atTheMiddle);
    const glm::mat4 viaArm = cameraWorldMatrix(glm::vec3(0), yaw90, atTheMiddle, 30.0f);
    CHECK(nearVec(forwardOf(viaObject), forwardOf(viaArm)));
    CHECK(nearVec(glm::vec3(viaObject * glm::vec4(0, 0, 1, 0)), glm::vec3(viaArm * glm::vec4(0, 0, 1, 0))));

    // A bad length is repaired.
    CameraSettings bad;
    bad.arm.targetArmLength = -4.0f;
    bad.sanitize();
    CHECK(bad.arm.targetArmLength == 0.0f);
    bad.arm.targetArmLength = std::numeric_limits<float>::quiet_NaN();
    bad.sanitize();
    CHECK(bad.arm.targetArmLength == 0.0f);
}

static void testViewFollowsTheCamera() {
    Camera camera;
    camera.setView(glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));  // at the origin, looking along +Y
    const glm::mat4 view = camera.getViewMatrix();
    auto inView = [&](glm::vec3 p) { return glm::vec3(view * glm::vec4(p, 1.0f)); };
    // In view space the camera looks down -Z, with +X to its right and +Y up.
    CHECK(nearVec(inView(glm::vec3(0, 5, 0)), glm::vec3(0, 0, -5)));  // straight ahead
    CHECK(nearVec(inView(glm::vec3(1, 5, 0)), glm::vec3(1, 0, -5)));  // to the right
    CHECK(nearVec(inView(glm::vec3(0, 5, 1)), glm::vec3(0, 1, -5)));  // above

    // Moved back and up, and turned to face -X.
    camera.setView(glm::vec3(4, 2, 5), glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1));
    const glm::mat4 view2 = camera.getViewMatrix();
    CHECK(nearVec(glm::vec3(view2 * glm::vec4(0, 2, 5, 1)), glm::vec3(0, 0, -4)));  // 4 units straight ahead
    CHECK(nearVec(camera.getPosition(), glm::vec3(4, 2, 5)));
    CHECK(near(camera.getYaw(), 180.0f, 0.01f) || near(camera.getYaw(), -180.0f, 0.01f));
    CHECK(near(camera.getPitch(), 0.0f));
}

static void testProjectionMatchesTheHorizontalFieldOfView() {
    Camera camera;
    camera.setView(glm::vec3(0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));
    const float aspect = 2.0f;
    camera.setProjection(verticalFovFromHorizontal(90.0f, aspect), 0.1f, 100.0f);
    const glm::mat4 viewProjection = camera.getProjectionMatrix(aspect) * camera.getViewMatrix();
    auto ndc = [&](glm::vec3 p) {
        glm::vec4 clip = viewProjection * glm::vec4(p, 1.0f);
        return glm::vec2(clip.x / clip.w, clip.y / clip.w);
    };
    // A 90 degree horizontal view: a point 5 ahead and 5 to the right is exactly at the right edge of the screen.
    CHECK(near(ndc(glm::vec3(5, 5, 0)).x, 1.0f, 1e-3f));
    CHECK(near(ndc(glm::vec3(-5, 5, 0)).x, -1.0f, 1e-3f));
    // Straight ahead is the middle. Above the middle is the top of the screen (Vulkan's y runs downward: negative).
    CHECK(near(ndc(glm::vec3(0, 5, 0)).x, 0.0f) && near(ndc(glm::vec3(0, 5, 0)).y, 0.0f));
    CHECK(ndc(glm::vec3(0, 5, 1)).y < 0.0f);
    // At aspect 2 the view is half as tall as it is wide, so 2.5 up at depth 5 is the screen's top edge.
    CHECK(near(ndc(glm::vec3(0, 5, 2.5f)).y, -1.0f, 1e-3f));
    CHECK(near(camera.getNearPlane(), 0.1f) && near(camera.getFarPlane(), 100.0f));
}

static void testDegenerateViews() {
    Camera camera;
    camera.setView(glm::vec3(1, 2, 3), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));
    camera.setView(glm::vec3(9, 9, 9), glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));  // no direction: ignored
    CHECK(nearVec(camera.getPosition(), glm::vec3(1, 2, 3)));

    camera.setView(glm::vec3(0), glm::vec3(0, 0, 1), glm::vec3(0, 0, 1));  // looking straight along "up"
    const glm::mat4 view = camera.getViewMatrix();
    CHECK(std::isfinite(view[0][0]) && std::isfinite(view[1][1]) && std::isfinite(view[2][2]));

    camera.setProjection(0.0f, -1.0f, -5.0f);  // nonsense values are pulled into range
    const glm::mat4 projection = camera.getProjectionMatrix(1.0f);
    CHECK(std::isfinite(projection[0][0]) && std::isfinite(projection[2][2]));
    CHECK(camera.getFarPlane() > camera.getNearPlane());
}

int main() {
    testVerticalFov();
    testCameraWorldMatrix();
    testSpringArm();
    testViewFollowsTheCamera();
    testProjectionMatchesTheHorizontalFieldOfView();
    testDegenerateViews();
    testHalfWidth();
    testDisplayDepth();
    testShape();
    testNearRectangle();
    testBadInput();
    testClip();
    if (failures == 0) std::printf("CameraFrustumTest: all passed\n");
    else std::printf("CameraFrustumTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
