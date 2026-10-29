// Tests for what a spring arm does in Play (include/core/SpringArm.h): the camera pulls in at solid objects, and the arm
// can lag behind its Object. Header only: the "world" is a handful of boxes made here.
//
//   cmake --build . --target SpringArmTest --config Debug
//   build/tests/SpringArmTest.exe

#include "core/SpringArm.h"
#include "physics/PlayPhysics.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Propulsion;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static bool approx(float a, float b, float tolerance = 1e-3f) { return std::fabs(a - b) <= tolerance; }
static bool approx(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-3f) { return glm::distance(a, b) <= tolerance; }
static glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }
static glm::vec3 forwardOf(const glm::mat4& m) { return glm::normalize(glm::vec3(m * glm::vec4(0, 1, 0, 0))); }

// ---- a small world: body 0 is the player, the others are whatever a test adds ----

struct World {
    std::vector<Physics::Body> bodies;
    std::vector<Physics::Pose> poses;

    World() { addCharacter(glm::vec3(0.0f)); }  // the player (index 0)

    int addCharacter(const glm::vec3& at) {
        Physics::Body b;
        b.character = true;
        b.solid = true;
        b.movement.radius = 0.4f;
        b.movement.halfHeight = 1.0f;
        Physics::Pose p;
        p.position = at;
        bodies.push_back(b);
        poses.push_back(p);
        return static_cast<int>(bodies.size()) - 1;
    }
    /// A solid box `size` across, centred at `at`.
    int addBox(const glm::vec3& at, const glm::vec3& size, bool solid = true) {
        Physics::Body b;
        b.hasShape = true;
        b.solid = solid;
        b.kind = Physics::ShapeKind::Box;
        b.boundsMin = glm::vec3(-0.5f);
        b.boundsMax = glm::vec3(0.5f);
        Physics::Pose p;
        p.position = at;
        p.scale = size;
        bodies.push_back(b);
        poses.push_back(p);
        return static_cast<int>(bodies.size()) - 1;
    }
    ArmProbe probe(int asker = 0, int* calls = nullptr) const {
        return [this, asker, calls](const glm::vec3& from, const glm::vec3& to, float radius) {
            if (calls) ++*calls;
            return Physics::clearFraction(bodies, poses, asker, from, to, radius);
        };
    }
};

/// A camera behind the player on a 5 long arm fixed at head height, as the player Object has.
static CameraSettings thirdPerson() {
    CameraSettings cam;
    cam.enabled = true;
    cam.arm.enabled = true;
    cam.arm.targetArmLength = 5.0f;
    cam.arm.targetOffset = glm::vec3(0, 0, 1.5f);
    return cam;
}

static const glm::quat kUpright(1, 0, 0, 0);

static glm::mat4 play(SpringArmState& state, const World& world, const CameraSettings& cam, float pitch = 0.0f, float dt = 0.016f,
                      const glm::quat& rotation = kUpright, int* calls = nullptr) {
    return playCameraMatrix(state, world.poses[0].position, rotation, cam, pitch, dt, world.probe(0, calls));
}

// ---- the arm's geometry is the same as it was before this existed ----

/// The camera matrix as it was written before the arm had a frame: the whole chain of transforms.
static glm::mat4 chainOfTransforms(const glm::vec3& pos, const glm::quat& rot, const CameraSettings& cam, float pitch) {
    glm::mat4 world = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
    if (cam.arm.enabled) {
        const glm::quat look = glm::angleAxis(glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        world = world * glm::translate(glm::mat4(1.0f), cam.arm.targetOffset) * glm::mat4_cast(look) *
                glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -cam.arm.targetArmLength, 0.0f)) *
                glm::translate(glm::mat4(1.0f), cam.arm.socketOffset);
    }
    return world * glm::translate(glm::mat4(1.0f), cam.offset) * glm::mat4_cast(glm::quat(glm::radians(cam.rotation)));
}

static void testTheFrameMatchesTheChainOfTransforms() {
    unsigned seed = 12345;
    auto next = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f * 2.0f - 1.0f;  // -1..1
    };
    for (int i = 0; i < 200; ++i) {
        CameraSettings cam;
        cam.enabled = true;
        cam.arm.enabled = (i % 4) != 0;
        cam.arm.targetArmLength = std::fabs(next()) * 8.0f;
        cam.arm.targetOffset = glm::vec3(next(), next(), next()) * 2.0f;
        cam.arm.socketOffset = glm::vec3(next(), next(), next()) * 2.0f;
        cam.offset = glm::vec3(next(), next(), next());
        cam.rotation = glm::vec3(next(), next(), next()) * 30.0f;
        const glm::vec3 pos = glm::vec3(next(), next(), next()) * 10.0f;
        const glm::quat rot = glm::quat(glm::radians(glm::vec3(0.0f, next() * 20.0f, next() * 180.0f)));
        const float pitch = next() * 80.0f;

        const glm::mat4 a = cameraWorldMatrix(pos, rot, cam, pitch);
        const glm::mat4 b = chainOfTransforms(pos, rot, cam, pitch);
        bool same = true;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) same = same && approx(a[c][r], b[c][r], 1e-3f);
        CHECK(same);
    }
}

static void testWithNothingInTheWayThePlayCameraIsTheEditorCamera() {
    World world;
    world.poses[0].position = glm::vec3(3, 4, 0);
    CameraSettings cam = thirdPerson();
    cam.arm.socketOffset = glm::vec3(0.5f, 0, 0.25f);
    SpringArmState state;
    const glm::quat turned = glm::quat(glm::radians(glm::vec3(0, 0, 40)));
    const glm::mat4 got = play(state, world, cam, 20.0f, 0.016f, turned);
    const glm::mat4 want = cameraWorldMatrix(world.poses[0].position, turned, cam, 20.0f);
    CHECK(approx(positionOf(got), positionOf(want)) && approx(forwardOf(got), forwardOf(want)));
}

// ---- collision ----

static void testAWallBehindPullsTheCameraIn() {
    World world;
    world.addBox(glm::vec3(0, -4, 1.5f), glm::vec3(20, 1, 20));  // its face is at y = -3.5, between the player and where the camera wants to be (y = -5)
    const CameraSettings cam = thirdPerson();
    SpringArmState state;
    const glm::mat4 m = play(state, world, cam);
    const glm::vec3 at = positionOf(m);
    CHECK(approx(at.y, -3.5f + cam.arm.probeSize, 0.02f));  // stopped a probe's radius from the wall
    CHECK(approx(at.x, 0.0f) && approx(at.z, 1.5f));          // straight along the arm
    CHECK(approx(forwardOf(m), glm::vec3(0, 1, 0)));         // still looking at where the arm is fixed
}

static void testAWallNotBehindLeavesTheArmAlone() {
    World world;
    world.addBox(glm::vec3(0, 6, 1.5f), glm::vec3(20, 1, 20));   // in front
    world.addBox(glm::vec3(0, -9, 1.5f), glm::vec3(20, 1, 20));  // behind, but past the end of the arm
    const CameraSettings cam = thirdPerson();
    SpringArmState state;
    CHECK(approx(positionOf(play(state, world, cam)), glm::vec3(0, -5, 1.5f)));
}

static void testTheCameraGoesBackOutWhenTheWayIsClear() {
    World world;
    const int wall = world.addBox(glm::vec3(0, -4, 1.5f), glm::vec3(20, 1, 20));
    const CameraSettings cam = thirdPerson();
    SpringArmState state;
    CHECK(positionOf(play(state, world, cam)).y > -3.5f);
    world.poses[wall].position = glm::vec3(0, -40, 1.5f);  // the player turned a corner, say
    CHECK(approx(positionOf(play(state, world, cam)).y, -5.0f));
}

static void testTheWallIsTestedFromTheArmsFixedPointNotTheObjectsFeet() {
    World world;
    world.addBox(glm::vec3(0, -4, 0.25f), glm::vec3(20, 1, 0.5f));  // a low step behind the player, under the arm (which is at head height)
    SpringArmState state;
    CHECK(approx(positionOf(play(state, world, thirdPerson())).y, -5.0f));
}

static void testTheArmFollowsTheLookDirection() {
    // Looking up swings the camera down toward the floor: it can't go below it.
    World world;
    world.addBox(glm::vec3(0, 0, -0.5f), glm::vec3(100, 100, 1));  // a floor whose top is at z = 0
    const CameraSettings cam = thirdPerson();
    SpringArmState state;
    const glm::vec3 up = positionOf(play(state, world, cam, 45.0f));  // 5 back and 45 degrees down would be z = 1.5 - 3.5 = -2
    CHECK(up.z > 0.0f && up.z < 0.5f);
    // Looking down swings it up and over: nothing there, so the full length.
    SpringArmState other;
    const glm::vec3 down = positionOf(play(other, world, cam, -45.0f));
    CHECK(approx(glm::distance(down, glm::vec3(0, 0, 1.5f)), 5.0f, 0.01f) && down.z > 1.5f);
}

static void testTheArmTurnsWithTheObject() {
    World world;
    world.addBox(glm::vec3(4, 0, 1.5f), glm::vec3(1, 20, 20));  // a wall to the player's right, face at x = 3.5
    const CameraSettings cam = thirdPerson();
    SpringArmState state;
    CHECK(approx(positionOf(play(state, world, cam)).y, -5.0f));  // facing +Y, the camera is behind (y = -5), not at the wall
    // Turned a quarter to the left (yaw +90 about Z, to look along -X), the camera is behind the player in +X, against that wall.
    SpringArmState turnedState;
    const glm::mat4 m = play(turnedState, world, cam, 0.0f, 0.016f, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1)));
    CHECK(approx(positionOf(m).x, 3.5f - cam.arm.probeSize, 0.02f));
}

static void testTheSocketOffsetIsTestedToo() {
    // An over-the-shoulder camera to the right, with a wall just to the right: the camera can't poke through it.
    World world;
    world.addBox(glm::vec3(2, 0, 1.5f), glm::vec3(1, 40, 20));  // face at x = 1.5
    CameraSettings cam = thirdPerson();
    cam.arm.socketOffset = glm::vec3(3.0f, 0, 0);  // wants to be at x = 3
    SpringArmState state;
    const glm::vec3 at = positionOf(play(state, world, cam));
    CHECK(at.x < 1.5f);
    CHECK(at.y < 0.0f);  // still behind
}

static void testCollisionCanBeSwitchedOff() {
    World world;
    world.addBox(glm::vec3(0, -4, 1.5f), glm::vec3(20, 1, 20));
    CameraSettings cam = thirdPerson();
    cam.arm.doCollisionTest = false;
    int calls = 0;
    SpringArmState state;
    CHECK(approx(positionOf(play(state, world, cam, 0.0f, 0.016f, kUpright, &calls)).y, -5.0f));
    CHECK(calls == 0);
}

static void testTheProbeSizeIsHowFarTheCameraStaysFromTheWall() {
    World world;
    world.addBox(glm::vec3(0, -4, 1.5f), glm::vec3(20, 1, 20));
    CameraSettings cam = thirdPerson();
    for (float size : {0.0f, 0.1f, 0.5f}) {
        cam.arm.probeSize = size;
        SpringArmState state;
        CHECK(approx(positionOf(play(state, world, cam)).y, -3.5f + size, 0.02f));
    }
}

static void testTheObjectAndCharactersAndNonSolidThingsDontPullTheCameraIn() {
    World world;
    world.addCharacter(glm::vec3(0, -3, 0));                          // someone standing behind the player
    world.addBox(glm::vec3(0, -4, 1.5f), glm::vec3(20, 1, 20), false);  // a non-solid wall (a trigger)
    SpringArmState state;
    CHECK(approx(positionOf(play(state, world, thirdPerson())).y, -5.0f));
}

static void testTheObjectsOwnBodyDoesNotCount() {
    // An arm fixed low enough to be inside the player's own capsule, and pointing back through it.
    World world;
    CameraSettings cam = thirdPerson();
    cam.arm.targetOffset = glm::vec3(0, 0, 0.5f);
    cam.arm.targetArmLength = 3.0f;
    SpringArmState state;
    CHECK(approx(positionOf(play(state, world, cam)).y, -3.0f));
    // An owner that isn't a character (a scripted box with a camera) is solid, and still not in its own way.
    World box;
    box.bodies[0].character = false;
    box.bodies[0].hasShape = true;
    box.bodies[0].solid = true;
    box.poses[0].scale = glm::vec3(2.0f);
    SpringArmState s3;
    CHECK(approx(positionOf(play(s3, box, cam)).y, -3.0f));
    // The same capsule belonging to someone else is a character, which the camera passes through as well.
    World crowd;
    crowd.poses[0].position = glm::vec3(0, 20, 0);  // the player is elsewhere
    crowd.addCharacter(glm::vec3(0, 20, 0.0f));
    SpringArmState s2;
    CHECK(approx(positionOf(play(s2, crowd, cam)).y, 17.0f));
}

static void testAFirstPersonCameraHasNothingToTest() {
    World world;
    world.addBox(glm::vec3(0, 0, 1.5f), glm::vec3(1, 1, 1));  // the eye is inside a box
    CameraSettings cam = thirdPerson();
    cam.arm.targetArmLength = 0.0f;
    int calls = 0;
    SpringArmState state;
    const glm::mat4 m = play(state, world, cam, 0.0f, 0.016f, kUpright, &calls);
    CHECK(calls == 0 && approx(positionOf(m), glm::vec3(0, 0, 1.5f)));
}

static void testAnArmStartingInsideSomethingPutsTheCameraAtItsFixedPoint() {
    World world;
    world.addBox(glm::vec3(0, 0, 1.5f), glm::vec3(1, 1, 1));
    SpringArmState state;
    CHECK(approx(positionOf(play(state, world, thirdPerson())), glm::vec3(0, 0, 1.5f), 0.01f));
}

static void testNoArmNoProbe() {
    World world;
    world.addBox(glm::vec3(0, 0, 0), glm::vec3(1, 1, 1));
    CameraSettings cam;
    cam.enabled = true;
    cam.offset = glm::vec3(0, -3, 2);
    int calls = 0;
    SpringArmState state;
    const glm::mat4 m = play(state, world, cam, 0.0f, 0.016f, kUpright, &calls);
    CHECK(calls == 0 && approx(positionOf(m), glm::vec3(0, -3, 2)));
}

static void testNoProbeMeansNoCollision() {
    CameraSettings cam = thirdPerson();
    SpringArmState state;
    const glm::mat4 m = playCameraMatrix(state, glm::vec3(0.0f), kUpright, cam, 0.0f, 0.016f, ArmProbe());
    CHECK(approx(positionOf(m), glm::vec3(0, -5, 1.5f)));
}

// ---- lag ----

static void testNoLagMeansTheArmIsWhereTheObjectIs() {
    World world;
    CameraSettings cam = thirdPerson();
    SpringArmState state;
    play(state, world, cam);
    world.poses[0].position = glm::vec3(10, 0, 0);
    CHECK(approx(positionOf(play(state, world, cam)).x, 10.0f));
}

static void testLagEasesTheFixedPointTowardTheObject() {
    World world;
    CameraSettings cam = thirdPerson();
    cam.arm.targetArmLength = 0.0f;  // so the camera is the fixed point
    cam.arm.enableCameraLag = true;
    cam.arm.cameraLagSpeed = 10.0f;
    SpringArmState state;
    play(state, world, cam);  // the first frame has nothing to ease from
    world.poses[0].position = glm::vec3(10, 0, 0);
    const float x = positionOf(play(state, world, cam, 0.0f, 0.1f)).x;
    CHECK(approx(x, 10.0f * (1.0f - std::exp(-1.0f)), 1e-3f));  // speed 10 for a tenth of a second closes 1 - e^-1 of the gap
    // And keeps closing it, never passing it.
    float last = x;
    for (int i = 0; i < 100; ++i) {
        const float now = positionOf(play(state, world, cam, 0.0f, 0.1f)).x;
        CHECK(now >= last && now <= 10.0f + 1e-4f);
        last = now;
    }
    CHECK(approx(last, 10.0f, 1e-3f));
}

static void testLagDoesNotDependOnTheFrameRate() {
    World world;
    CameraSettings cam = thirdPerson();
    cam.arm.targetArmLength = 0.0f;
    cam.arm.enableCameraLag = true;
    cam.arm.cameraLagSpeed = 6.0f;
    SpringArmState once, many;
    play(once, world, cam);
    play(many, world, cam);
    world.poses[0].position = glm::vec3(0, 8, 0);
    const float slow = positionOf(play(once, world, cam, 0.0f, 0.2f)).y;
    float fast = 0.0f;
    for (int i = 0; i < 20; ++i) fast = positionOf(play(many, world, cam, 0.0f, 0.01f)).y;
    CHECK(approx(slow, fast, 1e-3f));
}

static void testLagSpeedZeroMeansNoLag() {
    World world;
    CameraSettings cam = thirdPerson();
    cam.arm.targetArmLength = 0.0f;
    cam.arm.enableCameraLag = true;
    cam.arm.cameraLagSpeed = 0.0f;
    SpringArmState state;
    play(state, world, cam);
    world.poses[0].position = glm::vec3(5, 0, 0);
    CHECK(approx(positionOf(play(state, world, cam)).x, 5.0f));
}

static void testAFrameThatTakesNoTimeMovesNothing() {
    World world;
    CameraSettings cam = thirdPerson();
    cam.arm.targetArmLength = 0.0f;
    cam.arm.enableCameraLag = true;
    SpringArmState state;
    play(state, world, cam);
    world.poses[0].position = glm::vec3(5, 0, 0);
    CHECK(approx(positionOf(play(state, world, cam, 0.0f, 0.0f)).x, 0.0f));  // the game is paused
    CHECK(approx(positionOf(play(state, world, cam, 0.0f, -0.1f)).x, 0.0f));  // and time never runs backwards
}

static void testLagHasAMaximumDistance() {
    World world;
    CameraSettings cam = thirdPerson();
    cam.arm.targetArmLength = 0.0f;
    cam.arm.enableCameraLag = true;
    cam.arm.cameraLagSpeed = 0.5f;  // very slow
    cam.arm.cameraLagMaxDistance = 2.0f;
    SpringArmState state;
    play(state, world, cam);
    world.poses[0].position = glm::vec3(10, 0, 0);
    const float x = positionOf(play(state, world, cam, 0.0f, 0.016f)).x;
    CHECK(approx(x, 8.0f, 1e-3f));  // never more than 2 behind
}

static void testALongJumpIsATeleportAndTheArmDoesNotFlyAcrossTheLevel() {
    World world;
    CameraSettings cam = thirdPerson();
    cam.arm.targetArmLength = 0.0f;
    cam.arm.enableCameraLag = true;
    SpringArmState state;
    play(state, world, cam);
    world.poses[0].position = glm::vec3(500, 0, 0);
    CHECK(approx(positionOf(play(state, world, cam, 0.0f, 0.016f)).x, 500.0f));
}

static void testRotationLagTurnsTheArmAfterTheObject() {
    World world;
    CameraSettings cam = thirdPerson();
    cam.arm.enableCameraRotationLag = true;
    cam.arm.cameraRotationLagSpeed = 10.0f;
    SpringArmState state;
    play(state, world, cam);  // looking along +Y
    const glm::quat quarter = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));  // turn to look along -X
    const glm::mat4 m = play(state, world, cam, 0.0f, 0.1f, quarter);
    const float turned = glm::degrees(std::acos(glm::clamp(glm::dot(forwardOf(m), glm::vec3(0, 1, 0)), -1.0f, 1.0f)));
    CHECK(approx(turned, 90.0f * (1.0f - std::exp(-1.0f)), 0.05f));
    // The arm's fixed point is still on the Object: only the direction lags.
    CHECK(approx(positionOf(m) + forwardOf(m) * 5.0f, glm::vec3(0, 0, 1.5f), 0.01f));
    // And it gets there.
    glm::mat4 last = m;
    for (int i = 0; i < 100; ++i) last = play(state, world, cam, 0.0f, 0.1f, quarter);
    CHECK(approx(forwardOf(last), forwardOf(cameraWorldMatrix(glm::vec3(0.0f), quarter, cam, 0.0f)), 1e-3f));
}

static void testRotationLagTakesTheShortWayRound() {
    World world;
    CameraSettings cam = thirdPerson();
    cam.arm.enableCameraRotationLag = true;
    SpringArmState state;
    play(state, world, cam);
    // The same orientation as the one it has, written as the negated quaternion: no turning at all.
    const glm::mat4 m = play(state, world, cam, 0.0f, 0.1f, -kUpright);
    CHECK(approx(forwardOf(m), glm::vec3(0, 1, 0), 1e-3f));
}

static void testOnlyTheEnabledLagApplies() {
    World world;
    CameraSettings cam = thirdPerson();
    cam.arm.targetArmLength = 0.0f;
    cam.arm.enableCameraRotationLag = true;  // rotation lag only: moving the object isn't lagged
    SpringArmState state;
    play(state, world, cam);
    world.poses[0].position = glm::vec3(3, 0, 0);
    CHECK(approx(positionOf(play(state, world, cam, 0.0f, 0.016f)).x, 3.0f));
}

static void testTheWallStillWinsWhileLagging() {
    // The camera trails the player, who has now stepped back against the wall: the probe runs from where the arm
    // actually is, so the camera never ends up behind the wall.
    World world;
    world.addBox(glm::vec3(0, -4, 1.5f), glm::vec3(20, 1, 20));
    CameraSettings cam = thirdPerson();
    cam.arm.enableCameraLag = true;
    SpringArmState state;
    world.poses[0].position = glm::vec3(0, 3, 0);
    play(state, world, cam);
    world.poses[0].position = glm::vec3(0, 0, 0);
    for (int i = 0; i < 30; ++i) CHECK(positionOf(play(state, world, cam, 0.0f, 0.016f)).y > -3.5f);
}

int main() {
    testTheFrameMatchesTheChainOfTransforms();
    testWithNothingInTheWayThePlayCameraIsTheEditorCamera();
    testAWallBehindPullsTheCameraIn();
    testAWallNotBehindLeavesTheArmAlone();
    testTheCameraGoesBackOutWhenTheWayIsClear();
    testTheWallIsTestedFromTheArmsFixedPointNotTheObjectsFeet();
    testTheArmFollowsTheLookDirection();
    testTheArmTurnsWithTheObject();
    testTheSocketOffsetIsTestedToo();
    testCollisionCanBeSwitchedOff();
    testTheProbeSizeIsHowFarTheCameraStaysFromTheWall();
    testTheObjectAndCharactersAndNonSolidThingsDontPullTheCameraIn();
    testTheObjectsOwnBodyDoesNotCount();
    testAFirstPersonCameraHasNothingToTest();
    testAnArmStartingInsideSomethingPutsTheCameraAtItsFixedPoint();
    testNoArmNoProbe();
    testNoProbeMeansNoCollision();
    testNoLagMeansTheArmIsWhereTheObjectIs();
    testLagEasesTheFixedPointTowardTheObject();
    testLagDoesNotDependOnTheFrameRate();
    testLagSpeedZeroMeansNoLag();
    testAFrameThatTakesNoTimeMovesNothing();
    testLagHasAMaximumDistance();
    testALongJumpIsATeleportAndTheArmDoesNotFlyAcrossTheLevel();
    testRotationLagTurnsTheArmAfterTheObject();
    testRotationLagTakesTheShortWayRound();
    testOnlyTheEnabledLagApplies();
    testTheWallStillWinsWhileLagging();

    if (failures == 0) std::printf("SpringArmTest: all passed\n");
    return failures == 0 ? 0 : 1;
}
