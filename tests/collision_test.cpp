// Tests for collision shapes and the character mover (include/physics/Collision.h).
//
//   cmake --build . --target CollisionTest --config Debug
//   build/tests/CollisionTest.exe

#include "physics/PlayPhysics.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Propulsion::Physics;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

static glm::quat turn(float pitch, float roll, float yaw) { return glm::quat(glm::radians(glm::vec3(pitch, roll, yaw))); }

static Shape box(glm::vec3 center, glm::vec3 half, int id = -1, glm::quat rotation = glm::quat(1, 0, 0, 0)) {
    Shape s;
    s.kind = ShapeKind::Box;
    s.center = center;
    s.half = half;
    s.rotation = rotation;
    s.id = id;
    return s;
}

// A floor whose top is at z = 0, and a capsule (radius 0.5, 2 tall) that stands on it with its origin at z = 1.
static Shape floorShape(int id = 1) { return box(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f), id); }
static const float kStanding = 1.0f;

struct Walker {
    CharacterState state;
    CharacterParams params;
    std::vector<Shape> world;
    std::vector<int> touched;

    // One frame: `perSecond` is the move the script asked for, per second.
    void frame(glm::vec3 perSecond = glm::vec3(0.0f), bool jump = false, float dt = 1.0f / 60.0f) {
        moveCharacter(state, params, perSecond * dt, jump, dt, world, &touched);
    }
    void frames(int count, glm::vec3 perSecond = glm::vec3(0.0f), float dt = 1.0f / 60.0f) {
        for (int i = 0; i < count; ++i) frame(perSecond, false, dt);
    }
};

static void testFallsOntoTheFloorAndStaysThere() {
    Walker w;
    w.world = {floorShape()};
    w.state.position = glm::vec3(0, 0, 5);
    w.frames(120);
    CHECK(near(w.state.position.z, kStanding));
    CHECK(w.state.grounded);

    const float rest = w.state.position.z;
    float drift = 0.0f;
    for (int i = 0; i < 120; ++i) {
        w.frame();
        drift = (std::max)(drift, std::fabs(w.state.position.z - rest));
        CHECK(w.state.grounded);
    }
    CHECK(drift < 1e-3f);  // no sinking in, no bobbing
    CHECK(w.state.velocityZ == 0.0f);
}

static void testRestingHeightDoesNotDependOnTheFrameRate() {
    for (float dt : {1.0f / 20.0f, 1.0f / 30.0f, 1.0f / 144.0f, 0.1f}) {
        Walker w;
        w.world = {floorShape()};
        w.state.position = glm::vec3(0, 0, 4);
        w.frames(static_cast<int>(3.0f / dt), glm::vec3(0.0f), dt);
        CHECK(near(w.state.position.z, kStanding));
        CHECK(w.state.grounded);
    }
}

static void testWalkingStaysOnTheGround() {
    Walker w;
    w.world = {floorShape()};
    w.state.position = glm::vec3(0, 0, kStanding);
    w.frames(120, glm::vec3(0, 5, 0));
    CHECK(near(w.state.position.y, 10.0f, 0.05f));
    CHECK(near(w.state.position.z, kStanding, 0.002f));
    CHECK(w.state.grounded);
}

static void testAWallStopsTheCapsule() {
    Walker w;
    w.world = {floorShape(1), box(glm::vec3(0, 5, 1), glm::vec3(10, 0.5f, 2), 9)};  // its near face is at y = 4.5
    w.state.position = glm::vec3(0, 0, kStanding);
    w.frames(120, glm::vec3(0, 5, 0));
    CHECK(near(w.state.position.y, 4.0f));  // 4.5 minus the radius
    CHECK(near(w.state.position.z, kStanding));
    CHECK(std::find(w.touched.begin(), w.touched.end(), 9) != w.touched.end());  // it says what it ran into
    CHECK(std::find(w.touched.begin(), w.touched.end(), 1) != w.touched.end());  // and what it stands on

    // Pushing diagonally into the wall slides along it.
    const float x0 = w.state.position.x;
    w.frames(60, glm::vec3(3, 5, 0));
    CHECK(near(w.state.position.y, 4.0f));
    CHECK(w.state.position.x > x0 + 2.5f);
}

static void testSlidingAlongATurnedWall() {
    // The wall is turned 45 degrees, so it is hit at an angle: the capsule slides along it and never goes through.
    const glm::quat yaw = turn(0, 0, 45);
    Walker w;
    w.world = {floorShape(), box(glm::vec3(0, 5, 1), glm::vec3(10, 0.5f, 2), 9, yaw)};
    w.state.position = glm::vec3(0, 0, kStanding);
    glm::vec3 local;
    for (int i = 0; i < 180; ++i) {
        w.frame(glm::vec3(0, 5, 0));
        local = glm::conjugate(yaw) * (w.state.position - glm::vec3(0, 5, 1));
        CHECK(local.y < -0.99f);  // never past the wall's face (-0.5) less the radius (0.5)
    }
    CHECK(near(local.y, -1.0f, 0.02f));
    CHECK(local.x > 3.0f);  // it went along the wall
}

static void testWalkingOffAnEdgeFalls() {
    Walker w;
    // A platform with its top at z = 0, 10 wide each way, over a far floor whose top is at z = -5.
    w.world = {box(glm::vec3(0, 0, -0.5f), glm::vec3(10, 10, 0.5f), 1), box(glm::vec3(0, 0, -5.5f), glm::vec3(50, 50, 0.5f), 2)};
    w.state.position = glm::vec3(0, 0, kStanding);
    w.frames(60, glm::vec3(0, 3, 0));  // y = 3, still on it
    CHECK(w.state.grounded && near(w.state.position.z, kStanding, 0.002f));

    bool leftTheGround = false;
    float lowest = w.state.position.z;
    for (int i = 0; i < 240; ++i) {  // four more seconds of walking: the edge is at y = 10
        w.frame(glm::vec3(0, 3, 0));
        if (!w.state.grounded) leftTheGround = true;
        lowest = (std::min)(lowest, w.state.position.z);
    }
    CHECK(leftTheGround);                 // it walked off ...
    CHECK(w.state.position.y > 10.0f);    // ... the far edge
    CHECK(lowest < kStanding - 3.0f);     // and fell

    w.frames(240);
    CHECK(w.state.grounded);
    CHECK(near(w.state.position.z, -5.0f + kStanding));  // down on the far floor
}

static void testJumpGoesUpAndComesBackDown() {
    Walker w;
    w.world = {floorShape()};
    w.state.position = glm::vec3(0, 0, kStanding);
    w.frames(30);
    CHECK(w.state.grounded);

    w.frame(glm::vec3(0.0f), true);  // the jump button goes down
    CHECK(!w.state.grounded && w.state.velocityZ > 0.0f);
    float apex = w.state.position.z;
    bool landed = false;
    for (int i = 0; i < 120; ++i) {
        w.frame();
        apex = (std::max)(apex, w.state.position.z);
        if (w.state.grounded) landed = true;
    }
    const float expected = kStanding + w.params.jumpSpeed * w.params.jumpSpeed / (2.0f * w.params.gravity);  // v squared over 2g
    CHECK(near(apex, expected, 0.1f));
    CHECK(landed);
    CHECK(near(w.state.position.z, kStanding));
}

static void testJumpOnlyFromTheGround() {
    Walker w;
    w.world = {floorShape()};
    w.state.position = glm::vec3(0, 0, kStanding);
    w.frames(30);
    float apex = 0.0f;
    for (int i = 0; i < 70; ++i) {  // holding the button down through the whole jump
        w.frame(glm::vec3(0.0f), i < 35);
        apex = (std::max)(apex, w.state.position.z);
    }
    const float expected = kStanding + w.params.jumpSpeed * w.params.jumpSpeed / (2.0f * w.params.gravity);
    CHECK(apex < expected + 0.1f);  // no second jump in mid-air

    Walker air;  // a character that was never on the ground can't jump either
    air.world = {floorShape()};
    air.state.position = glm::vec3(0, 0, 6);
    air.frame(glm::vec3(0.0f), true);
    CHECK(air.state.velocityZ < 0.0f);
}

static void testACeilingStopsAJump() {
    Walker w;
    w.world = {floorShape(), box(glm::vec3(0, 0, 3), glm::vec3(10, 10, 0.5f), 5)};  // its underside is at z = 2.5
    w.state.position = glm::vec3(0, 0, kStanding);
    w.frames(30);
    w.frame(glm::vec3(0.0f), true);
    float apex = w.state.position.z;
    int framesAtTheCeiling = 0;
    for (int i = 0; i < 120; ++i) {
        w.frame();
        apex = (std::max)(apex, w.state.position.z);
        if (w.state.position.z > 2.5f - w.params.halfHeight - 0.01f) ++framesAtTheCeiling;
    }
    CHECK(framesAtTheCeiling > 0 && framesAtTheCeiling <= 3);  // it bounces off, and doesn't hang there with its head against it
    CHECK(apex < 2.5f - w.params.halfHeight + 0.01f);  // the head stopped at the ceiling
    CHECK(w.state.grounded && near(w.state.position.z, kStanding));  // and it came back down, not stuck up there
    CHECK(std::find(w.touched.begin(), w.touched.end(), 5) != w.touched.end());
}

static void testAThinWallStopsAFastCapsule() {
    Walker w;
    w.world = {floorShape(), box(glm::vec3(0, 5, 1), glm::vec3(10, 0.05f, 2), 9)};  // a wall 0.1 thick, its near face at y = 4.95
    w.state.position = glm::vec3(0, 0, kStanding);
    w.frames(10, glm::vec3(0, 40, 0), 0.1f);  // 4 units per frame, many times the wall's thickness
    CHECK(w.state.position.y < 4.95f - w.params.radius + 0.01f);
}

static void testTheWholeBodyCounts() {
    // A beam at the height of the capsule's upper body, well clear of its feet and of its middle.
    Walker w;
    w.world = {floorShape(), box(glm::vec3(0, 5, 1.7f), glm::vec3(10, 0.5f, 0.1f), 9)};
    w.state.position = glm::vec3(0, 0, kStanding);
    w.frames(120, glm::vec3(0, 5, 0));
    // The beam's face is at y = 4.5. The top ball (center z = 1.5, 0.1 under the beam) touches it a little before a
    // full radius away: sqrt(0.5 squared - 0.1 squared) = 0.49.
    CHECK(near(w.state.position.y, 4.5f - 0.49f, 0.02f));

    // A thin beam at the height of the capsule's middle: the sides are as round as a capsule's, with no gaps to slip through.
    Walker mid;
    mid.world = {floorShape(), box(glm::vec3(0, 5, 1.0f), glm::vec3(10, 0.5f, 0.02f), 9)};
    mid.state.position = glm::vec3(0, 0, kStanding);
    mid.frames(120, glm::vec3(0, 5, 0));
    CHECK(near(mid.state.position.y, 4.0f, 0.02f));
}

static void testARampIsWalkable() {
    // A slab turned 30 degrees about X: its top slopes up toward +Y.
    Walker w;
    w.world = {box(glm::vec3(0, 0, 0), glm::vec3(10, 30, 0.5f), 3, turn(30, 0, 0))};
    w.state.position = glm::vec3(0, 0, 4);
    w.frames(120);
    CHECK(w.state.grounded);

    const glm::vec3 rest = w.state.position;  // standing on the slope does not slide down it
    w.frames(180);
    CHECK(near(w.state.position.x, rest.x, 0.001f) && near(w.state.position.y, rest.y, 0.001f) && near(w.state.position.z, rest.z, 0.001f));

    const glm::vec3 start = w.state.position;  // and walking up it climbs it
    w.frames(60, glm::vec3(0, 4, 0));
    CHECK(w.state.grounded);
    const float rise = w.state.position.z - start.z, run = w.state.position.y - start.y;
    CHECK(run > 3.0f && near(rise / run, std::tan(glm::radians(30.0f)), 0.05f));
}

static void testASteepSlopeIsAWall() {
    Walker w;
    w.world = {box(glm::vec3(0, 0, 0), glm::vec3(10, 30, 0.5f), 3, turn(60, 0, 0))};  // steeper than the 45 degree limit
    w.state.position = glm::vec3(0, 0, 4);
    w.frames(90);
    CHECK(!w.state.grounded);
    const float y = w.state.position.y;
    w.frames(30);
    CHECK(w.state.position.y < y - 0.1f);  // it slides down
    CHECK(!w.state.grounded);
}

static void testSphereAndCylinderShapes() {
    {   // standing on the top of a ball of radius 2: the lowest part of the capsule is a cap 0.5 wide, 0.5 below the middle
        Shape ball;
        ball.kind = ShapeKind::Sphere;
        ball.half = glm::vec3(2.0f);
        Walker w;
        w.world = {ball};
        w.state.position = glm::vec3(0, 0, 6);
        w.frames(120);
        CHECK(w.state.grounded && near(w.state.position.z, 3.0f, 0.02f));
    }
    {   // a cylinder stood on its end (its length runs along its own Y axis, so it is turned a quarter about X)
        Shape drum;
        drum.kind = ShapeKind::Cylinder;
        drum.half = glm::vec3(2.0f, 1.0f, 2.0f);
        drum.center = glm::vec3(0, 0, -1);
        drum.rotation = turn(90, 0, 0);
        Walker w;
        w.world = {drum};
        w.state.position = glm::vec3(0, 0, 5);
        w.frames(120);
        CHECK(w.state.grounded && near(w.state.position.z, kStanding));
        w.frames(120, glm::vec3(3, 0, 0));  // walks off the side, and falls
        CHECK(!w.state.grounded && w.state.position.z < -3.0f);
    }
    {   // a cylinder lying as drawn (length along Y): its round side is the way up
        Shape log;
        log.kind = ShapeKind::Cylinder;
        log.half = glm::vec3(1.0f, 5.0f, 1.0f);
        Walker w;
        w.world = {log};
        w.state.position = glm::vec3(0, 0, 5);
        w.frames(120);
        CHECK(w.state.grounded && near(w.state.position.z, 2.0f, 0.02f));  // its top is at z = 1
    }
}

static void testShapesComeFromMeshBounds() {
    const glm::quat yaw90 = turn(0, 0, 90);
    // The default meshes span -1..1; an object scales them and turns them.
    Shape cube = shapeFromMesh(ShapeKind::Box, glm::vec3(-1), glm::vec3(1), glm::vec3(1, 2, 3), yaw90, glm::vec3(2, 3, 0.5f), 4);
    CHECK(cube.id == 4 && cube.center == glm::vec3(1, 2, 3));
    CHECK(near(cube.half.x, 2.0f) && near(cube.half.y, 3.0f) && near(cube.half.z, 0.5f));
    // A mesh that isn't centered on its origin moves the shape with the object's turn and scale.
    Shape off = shapeFromMesh(ShapeKind::Box, glm::vec3(0), glm::vec3(2), glm::vec3(10, 0, 0), yaw90, glm::vec3(2));
    CHECK(near(off.center.x, 8.0f) && near(off.center.y, 2.0f) && near(off.center.z, 2.0f));
    // A ball is as big as the largest scale; a cylinder's width follows the larger of its two cross-section scales.
    Shape ball = shapeFromMesh(ShapeKind::Sphere, glm::vec3(-1), glm::vec3(1), glm::vec3(0), yaw90, glm::vec3(1, 2, 3));
    CHECK(near(ball.half.x, 3.0f) && ball.half.x == ball.half.y && ball.half.y == ball.half.z);
    Shape drum = shapeFromMesh(ShapeKind::Cylinder, glm::vec3(-1), glm::vec3(1), glm::vec3(0), yaw90, glm::vec3(1, 2, 3));
    CHECK(near(drum.half.x, 3.0f) && near(drum.half.y, 2.0f) && near(drum.half.z, 3.0f));
    // A negative scale flips the mesh, not its size.
    Shape flipped = shapeFromMesh(ShapeKind::Box, glm::vec3(-1), glm::vec3(1), glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(-2, 1, 1));
    CHECK(near(flipped.half.x, 2.0f));
}

static void testPushOutOfShape() {
    glm::vec3 n;
    float d;
    Shape unit = box(glm::vec3(0), glm::vec3(1));
    CHECK(pushOutOfShape(unit, glm::vec3(1.2f, 0, 0), 0.5f, n, d));  // overlapping a side: out through it
    CHECK(near(n.x, 1.0f) && near(n.y, 0.0f) && near(d, 0.3f));
    CHECK(!pushOutOfShape(unit, glm::vec3(1.6f, 0, 0), 0.5f, n, d));  // just clear
    CHECK(pushOutOfShape(unit, glm::vec3(0.9f, 0, 0), 0.5f, n, d));    // center inside: out the nearest face
    CHECK(near(n.x, 1.0f) && near(d, 0.6f));
    CHECK(pushOutOfShape(unit, glm::vec3(1.2f, 1.2f, 0), 0.5f, n, d)); // over an edge: out along the diagonal
    CHECK(near(n.x, n.y) && n.x > 0.6f && near(d, 0.5f - std::sqrt(0.08f)));

    Shape turned = box(glm::vec3(0), glm::vec3(1, 1, 1), -1, turn(0, 0, 90));  // the same box turned: still a box
    CHECK(pushOutOfShape(turned, glm::vec3(0, 1.2f, 0), 0.5f, n, d));
    CHECK(near(n.y, 1.0f) && near(d, 0.3f));

    Shape ball;
    ball.kind = ShapeKind::Sphere;
    ball.half = glm::vec3(1.0f);
    CHECK(pushOutOfShape(ball, glm::vec3(0, 0, 1.2f), 0.5f, n, d) && near(n.z, 1.0f) && near(d, 0.3f));
    CHECK(!pushOutOfShape(ball, glm::vec3(0, 0, 1.6f), 0.5f, n, d));
    CHECK(pushOutOfShape(ball, glm::vec3(0.0f), 0.5f, n, d) && near(glm::length(n), 1.0f));  // dead center still goes somewhere

    Shape drum;
    drum.kind = ShapeKind::Cylinder;
    drum.half = glm::vec3(1.0f, 2.0f, 1.0f);
    CHECK(pushOutOfShape(drum, glm::vec3(1.2f, 0, 0), 0.5f, n, d) && near(n.x, 1.0f) && near(d, 0.3f));   // the side
    CHECK(pushOutOfShape(drum, glm::vec3(0, 2.2f, 0), 0.5f, n, d) && near(n.y, 1.0f) && near(d, 0.3f));   // an end
    CHECK(!pushOutOfShape(drum, glm::vec3(1.6f, 0, 0), 0.5f, n, d));
    CHECK(!pushOutOfShape(drum, glm::vec3(0, 2.6f, 0), 0.5f, n, d));
    CHECK(!pushOutOfShape(drum, glm::vec3(1.4f, 2.4f, 0), 0.5f, n, d));  // past the rim, and the corner is further than the ball reaches
    CHECK(pushOutOfShape(drum, glm::vec3(1.2f, 2.2f, 0), 0.5f, n, d));   // over the rim: out along the diagonal
    CHECK(near(n.x, n.y) && n.x > 0.6f && near(d, 0.5f - std::sqrt(0.08f)));
}

static void testEmptyWorldAndFastFall() {
    Walker w;
    w.state.position = glm::vec3(0, 0, 0);
    w.frames(600);  // ten seconds with nothing below
    CHECK(!w.state.grounded);
    CHECK(near(w.state.velocityZ, -kTerminalSpeed));
}

static void testASlimCapsuleIsOneBall() {
    Walker w;  // a character no taller than it is wide is a single ball
    w.params.radius = 0.5f;
    w.params.halfHeight = 0.5f;
    w.world = {floorShape()};
    w.state.position = glm::vec3(0, 0, 3);
    w.frames(120);
    CHECK(w.state.grounded && near(w.state.position.z, 0.5f));
}

// ---- Distances, traces and overlaps ----

static Shape sphereShape(glm::vec3 center, float radius, int id = -1) {
    Shape s;
    s.kind = ShapeKind::Sphere;
    s.center = center;
    s.half = glm::vec3(radius);
    s.id = id;
    return s;
}

static Shape cylinderShape(glm::vec3 center, float radius, float halfLength, int id = -1, glm::quat rotation = glm::quat(1, 0, 0, 0)) {
    Shape s;
    s.kind = ShapeKind::Cylinder;
    s.center = center;
    s.half = glm::vec3(radius, halfLength, radius);
    s.rotation = rotation;
    s.id = id;
    return s;
}

static void testSignedDistance() {
    Shape b = box(glm::vec3(0), glm::vec3(1, 2, 3));
    CHECK(near(signedDistance(b, glm::vec3(3, 0, 0)), 2.0f));
    CHECK(near(signedDistance(b, glm::vec3(0, 0, 0)), -1.0f));  // inside: the way out is 1 (through the nearest face)
    CHECK(near(signedDistance(b, glm::vec3(2, 3, 4)), std::sqrt(3.0f)));  // off a corner
    b.rotation = turn(0, 0, 90);                                 // turned: its 1-wide side now faces +Y, its 2-wide side +X
    CHECK(near(signedDistance(b, glm::vec3(3, 0, 0)), 1.0f));
    CHECK(near(signedDistance(sphereShape(glm::vec3(0), 2.0f), glm::vec3(5, 0, 0)), 3.0f));
    Shape drum = cylinderShape(glm::vec3(0), 1.0f, 2.0f);        // its length runs along Y
    CHECK(near(signedDistance(drum, glm::vec3(0, 5, 0)), 3.0f));
    CHECK(near(signedDistance(drum, glm::vec3(3, 0, 0)), 2.0f));
    CHECK(near(signedDistance(drum, glm::vec3(2, 3, 0)), std::sqrt(2.0f)));
    CHECK(near(signedDistance(drum, glm::vec3(0, 0, 0)), -1.0f));
    Shape pill = capsuleShape(glm::vec3(0), 0.5f, 1.0f);
    CHECK(near(signedDistance(pill, glm::vec3(0, 0, 3)), 2.0f));
    CHECK(near(signedDistance(pill, glm::vec3(2, 0, 0)), 1.5f));
    CHECK(near(signedDistance(pill, glm::vec3(0, 0, 0.4f)), -0.5f));
}

static void testTraces() {
    const std::vector<Shape> level = {floorShape(1), box(glm::vec3(0, 5, 1), glm::vec3(10, 0.5f, 2), 9)};  // the wall's near face is at y = 4.5

    TraceHit hit = traceShapes(level, glm::vec3(0, 0, 5), glm::vec3(0, 0, -5));  // straight down onto the floor
    CHECK(hit.blocked && hit.id == 1);
    CHECK(near(hit.distance, 5.0f) && near(hit.location.z, 0.0f) && near(hit.normal.z, 1.0f));

    hit = traceShapes(level, glm::vec3(0, 0, 1), glm::vec3(0, 10, 1));  // along the ground into the wall
    CHECK(hit.blocked && hit.id == 9 && near(hit.distance, 4.5f) && near(hit.location.y, 4.5f));
    CHECK(near(hit.normal.y, -1.0f) && near(hit.normal.z, 0.0f));

    hit = traceShapes(level, glm::vec3(0, 0, 5), glm::vec3(0, 0, 2));  // stops short
    CHECK(!hit.blocked && hit.id == -1 && hit.location == glm::vec3(0, 0, 2) && near(hit.distance, 3.0f));

    hit = traceShapes(level, glm::vec3(0, 0, 5), glm::vec3(0, 0, 1.0f), 1.0f);  // a ball of radius 1 stops 1 above the floor
    CHECK(hit.blocked && near(hit.distance, 4.0f) && near(hit.location.z, 0.0f));
    hit = traceShapes(level, glm::vec3(0, 0, 5), glm::vec3(0, 0, 1.5f), 1.0f);  // ... and wouldn't get there
    CHECK(!hit.blocked);

    hit = traceShapes(level, glm::vec3(0, 5, 1), glm::vec3(0, 20, 1));  // starting inside the wall: hit at once
    CHECK(hit.blocked && hit.id == 9 && hit.distance == 0.0f);

    hit = traceShapes(level, glm::vec3(0, 0, 1), glm::vec3(0, 0, 1));  // no length, no hit
    CHECK(!hit.blocked);
    CHECK(!traceShapes({}, glm::vec3(0), glm::vec3(0, 10, 0)).blocked);

    // The nearest of several things, whatever order they are in.
    const std::vector<Shape> two = {box(glm::vec3(0, 20, 0), glm::vec3(5), 2), box(glm::vec3(0, 10, 0), glm::vec3(1), 3)};
    hit = traceShapes(two, glm::vec3(0, 0, 0), glm::vec3(0, 50, 0));
    CHECK(hit.blocked && hit.id == 3 && near(hit.distance, 9.0f));

    // A turned wall faces diagonally.
    const std::vector<Shape> turned = {box(glm::vec3(0, 5, 1), glm::vec3(10, 0.5f, 2), 4, turn(0, 0, 45))};
    hit = traceShapes(turned, glm::vec3(0, 0, 1), glm::vec3(0, 10, 1));
    CHECK(hit.blocked && near(hit.normal.x, 0.7071f, 0.02f) && near(hit.normal.y, -0.7071f, 0.02f));

    // Balls, drums and capsules.
    hit = traceShapes({sphereShape(glm::vec3(0, 10, 0), 2.0f, 6)}, glm::vec3(0, 0, 0), glm::vec3(0, 20, 0));
    CHECK(hit.blocked && hit.id == 6 && near(hit.distance, 8.0f) && near(hit.normal.y, -1.0f));
    hit = traceShapes({cylinderShape(glm::vec3(0, 10, 0), 2.0f, 1.0f, 7)}, glm::vec3(0, 0, 0), glm::vec3(0, 20, 0));  // its flat end faces us
    CHECK(hit.blocked && near(hit.distance, 9.0f));
    hit = traceShapes({capsuleShape(glm::vec3(0, 10, 0), 0.5f, 1.0f, 8)}, glm::vec3(-5, 10, 0), glm::vec3(5, 10, 0));
    CHECK(hit.blocked && near(hit.distance, 4.5f) && near(hit.normal.x, -1.0f));

    // A line skimming just above the floor doesn't hit it, and still finds the wall at the end.
    hit = traceShapes(level, glm::vec3(-8, 0, 0.2f), glm::vec3(8, 0, 0.2f));
    CHECK(!hit.blocked);
    hit = traceShapes(level, glm::vec3(0, -8, 0.2f), glm::vec3(0, 8, 0.2f));
    CHECK(hit.blocked && hit.id == 9 && near(hit.distance, 12.5f, 0.02f));
}

// Whether two boxes overlap, by the separating axis test (an independent check for the general method).
static bool boxesOverlapSAT(const Shape& a, const Shape& b) {
    const glm::mat3 ra(a.rotation), rb(b.rotation);
    const glm::vec3 t = b.center - a.center;
    auto separated = [&](glm::vec3 axis) {
        if (glm::dot(axis, axis) < 1e-8f) return false;  // (two parallel edges: no axis)
        axis = glm::normalize(axis);
        float ea = 0.0f, eb = 0.0f;
        for (int i = 0; i < 3; ++i) {
            ea += a.half[i] * std::fabs(glm::dot(ra[i], axis));
            eb += b.half[i] * std::fabs(glm::dot(rb[i], axis));
        }
        return std::fabs(glm::dot(t, axis)) > ea + eb;
    };
    for (int i = 0; i < 3; ++i) {
        if (separated(ra[i]) || separated(rb[i])) return false;
        for (int j = 0; j < 3; ++j) {
            if (separated(glm::cross(ra[i], rb[j]))) return false;
        }
    }
    return true;
}

static void testOverlaps() {
    {   // two long planks crossing off-center: no corner or middle of either is inside the other
        Shape a = box(glm::vec3(0, 0, 0), glm::vec3(5, 0.1f, 0.1f));
        Shape b = box(glm::vec3(4, 3, 0), glm::vec3(0.1f, 5, 0.1f));
        CHECK(shapesOverlap(a, b) && shapesOverlap(b, a));
        b.center.z = 1.0f;  // lifted clear of it
        CHECK(!shapesOverlap(a, b) && !shapesOverlap(b, a));
        CHECK(shapesOverlap(a, b, 1.0f));  // unless a gap this big counts
    }
    {   // resting against each other counts as touching, a real gap doesn't
        Shape a = box(glm::vec3(0), glm::vec3(1));
        CHECK(shapesOverlap(a, box(glm::vec3(2.0f, 0, 0), glm::vec3(1)), 0.02f));
        CHECK(!shapesOverlap(a, box(glm::vec3(2.1f, 0, 0), glm::vec3(1)), 0.02f));
        CHECK(shapesOverlap(a, box(glm::vec3(0.1f), glm::vec3(0.1f))));  // one inside the other
    }
    {   // a ball and a drum, a drum and a drum, a capsule and a box
        CHECK(shapesOverlap(sphereShape(glm::vec3(1.4f, 0, 0), 0.5f), cylinderShape(glm::vec3(0), 1.0f, 2.0f)));
        CHECK(!shapesOverlap(sphereShape(glm::vec3(1.6f, 0, 0), 0.5f), cylinderShape(glm::vec3(0), 1.0f, 2.0f)));
        CHECK(shapesOverlap(cylinderShape(glm::vec3(0), 1.0f, 2.0f), cylinderShape(glm::vec3(1.8f, 0, 0), 1.0f, 2.0f)));
        CHECK(!shapesOverlap(cylinderShape(glm::vec3(0), 1.0f, 2.0f), cylinderShape(glm::vec3(2.2f, 0, 0), 1.0f, 2.0f)));
        CHECK(shapesOverlap(cylinderShape(glm::vec3(0), 1.0f, 2.0f), cylinderShape(glm::vec3(0, 3.5f, 0), 1.0f, 2.0f)));   // end to end
        CHECK(!shapesOverlap(cylinderShape(glm::vec3(0), 1.0f, 2.0f), cylinderShape(glm::vec3(0, 4.5f, 0), 1.0f, 2.0f)));
        CHECK(shapesOverlap(capsuleShape(glm::vec3(0, 0, 1), 0.5f, 1.0f), floorShape()));             // standing in the floor
        CHECK(!shapesOverlap(capsuleShape(glm::vec3(0, 0, 1.5f), 0.5f, 1.0f), floorShape()));         // standing above it
        CHECK(shapesOverlap(capsuleShape(glm::vec3(0, 0, 1.5f), 0.5f, 1.0f), floorShape(), 0.6f));
    }

    // Against methods we trust: boxes against the separating axis test, and balls and capsules against the distance to
    // the other shape. Cases within a hair of touching are skipped (they could go either way).
    unsigned seed = 12345;
    auto random = [&](float low, float high) {
        seed = seed * 1664525u + 1013904223u;
        return low + (high - low) * static_cast<float>(seed >> 8) / 16777216.0f;
    };
    auto randomShape = [&](ShapeKind kind) {
        Shape s;
        s.kind = kind;
        s.center = glm::vec3(random(-3, 3), random(-3, 3), random(-3, 3));
        const glm::vec4 q(random(-1, 1), random(-1, 1), random(-1, 1), random(0.1f, 1));
        s.rotation = glm::normalize(glm::quat(q.w, q.x, q.y, q.z));
        s.half = glm::vec3(random(0.2f, 2), random(0.2f, 2), random(0.2f, 2));
        if (kind == ShapeKind::Sphere) s.half = glm::vec3(s.half.x);
        if (kind == ShapeKind::Cylinder) s.half.z = s.half.x;
        if (kind == ShapeKind::Capsule) { s.half.y = (std::max)(s.half.y, s.half.x); s.rotation = glm::quat(1, 0, 0, 0); }
        return s;
    };
    int compared = 0;
    for (int i = 0; i < 400; ++i) {
        const Shape a = randomShape(ShapeKind::Box), b = randomShape(ShapeKind::Box);
        CHECK(shapesOverlap(a, b) == boxesOverlapSAT(a, b));
        CHECK(shapesOverlap(a, b) == shapesOverlap(b, a));
        ++compared;
    }
    for (ShapeKind other : {ShapeKind::Box, ShapeKind::Sphere, ShapeKind::Cylinder, ShapeKind::Capsule}) {
        for (int i = 0; i < 300; ++i) {
            const Shape ball = randomShape(ShapeKind::Sphere), x = randomShape(other);
            const float margin = signedDistance(x, ball.center) - ball.half.x;
            if (std::fabs(margin) < 0.02f) continue;
            CHECK(shapesOverlap(ball, x) == (margin < 0.0f));
            CHECK(shapesOverlap(x, ball) == (margin < 0.0f));

            const Shape pill = randomShape(ShapeKind::Capsule);  // the nearest point of the capsule's spine to x
            const float segment = (std::max)(0.0f, pill.half.y - pill.half.x);
            float nearest = 1e9f;
            for (int k = 0; k <= 400; ++k) {
                const glm::vec3 spine = pill.center + glm::vec3(0, 0, -segment + 2.0f * segment * static_cast<float>(k) / 400.0f);
                nearest = (std::min)(nearest, signedDistance(x, spine));
            }
            const float gap = nearest - pill.half.x;
            if (std::fabs(gap) < 0.03f) continue;
            CHECK(shapesOverlap(pill, x) == (gap < 0.0f));
            ++compared;
        }
    }
    CHECK(compared > 1000);
}

static void testCapsulesAsObstacles() {
    glm::vec3 n;
    float d;
    const Shape pill = capsuleShape(glm::vec3(0), 0.5f, 1.0f);  // a spine from z = -0.5 to 0.5
    CHECK(pushOutOfShape(pill, glm::vec3(0.8f, 0, 0), 0.5f, n, d) && near(n.x, 1.0f) && near(d, 0.2f));  // by the side
    CHECK(pushOutOfShape(pill, glm::vec3(0, 0, 1.2f), 0.5f, n, d) && near(n.z, 1.0f) && near(d, 0.3f));  // over the top
    CHECK(!pushOutOfShape(pill, glm::vec3(1.1f, 0, 0), 0.5f, n, d));
    CHECK(!pushOutOfShape(pill, glm::vec3(0, 0, 1.6f), 0.5f, n, d));

    // A character can stand on another character's head, and walk into its side.
    Walker w;
    w.world = {floorShape(), capsuleShape(glm::vec3(0, 5, 1), 0.5f, 1.0f, 9)};
    w.state.position = glm::vec3(0, 0, 1);
    w.frames(180, glm::vec3(0, 5, 0));
    CHECK(near(w.state.position.y, 4.0f, 0.05f));  // stopped by the capsule's side: 5 - 0.5 - 0.5
}

// ---- The scene's objects as the engine steps them each frame (include/physics/PlayPhysics.h) ----

struct Level {
    std::vector<Body> bodies;
    std::vector<Pose> poses;
    Contacts contacts;
    // Every pair that began touching, ended touching and hit, since the level was made.
    std::vector<Contacts::Pair> began, ended, hit;

    void script(size_t i) { bodies[i].scripted = true; }

    // Add an object whose mesh spans -1..1 (the default meshes), so its scale is its half-size. Returns its index.
    size_t add(glm::vec3 position, glm::vec3 scale, bool solid = true) {
        Body b;
        b.hasShape = true;
        b.solid = solid;
        b.boundsMin = glm::vec3(-1.0f);
        b.boundsMax = glm::vec3(1.0f);
        Pose p;
        p.position = position;
        p.scale = scale;
        bodies.push_back(b);
        poses.push_back(p);
        return bodies.size() - 1;
    }
    size_t addCharacter(glm::vec3 position, glm::vec3 scale = glm::vec3(1.0f)) {
        const size_t i = add(position, scale);
        bodies[i].character = true;
        bodies[i].movement.enabled = true;  // radius 0.5, half height 1, the defaults
        bodies[i].state.position = position;
        return i;
    }
    // One frame the way the engine does it: the script asks for a move, then the physics decides.
    std::vector<size_t> frame(size_t who, glm::vec3 scriptMove, bool jump = false, float dt = 1.0f / 60.0f) {
        poses[who].position += scriptMove;
        std::vector<size_t> moved;
        // The jump button is down for everyone's `jump` action (each test has one character, or wants them all to react).
        stepBodies(bodies, poses, dt, [&](size_t, const std::string& action) { return jump && action == "jump"; }, &moved, &contacts);
        began.insert(began.end(), contacts.began.begin(), contacts.began.end());
        ended.insert(ended.end(), contacts.ended.begin(), contacts.ended.end());
        hit.insert(hit.end(), contacts.hit.begin(), contacts.hit.end());
        return moved;
    }
};

static int countOf(const std::vector<Contacts::Pair>& events, size_t a, size_t b) {
    return static_cast<int>(std::count(events.begin(), events.end(), Contacts::Pair((std::min)(a, b), (std::max)(a, b))));
}

static void testAScriptedCharacterStopsAtASolidWall() {
    Level level;
    level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));    // floor, top at 0
    level.add(glm::vec3(0, 5, 1), glm::vec3(10, 0.5f, 2));         // wall, near face at y = 4.5
    const size_t player = level.addCharacter(glm::vec3(0, 0, 1));
    for (int i = 0; i < 180; ++i) level.frame(player, glm::vec3(0, 5.0f / 60.0f, 0));  // the script walks forward at 5 a second
    CHECK(near(level.poses[player].position.y, 4.0f));
    CHECK(near(level.poses[player].position.z, 1.0f));
    CHECK(level.poses[player].position == level.bodies[player].state.position);  // the object is where the physics left it
}

static void testOnlyCharactersAreMoved() {
    Level level;
    const size_t floorIndex = level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
    const size_t crate = level.add(glm::vec3(3, 3, 1), glm::vec3(1.0f));
    const size_t player = level.addCharacter(glm::vec3(0, 0, 5));
    const std::vector<size_t> moved = level.frame(player, glm::vec3(0.0f));
    CHECK(moved.size() == 1 && moved[0] == player);
    CHECK(level.poses[floorIndex].position == glm::vec3(0, 0, -0.5f));
    CHECK(level.poses[crate].position == glm::vec3(3, 3, 1));
}

static void testCharactersFallOnTheirOwn() {
    Level level;
    level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
    const size_t player = level.addCharacter(glm::vec3(0, 0, 6));
    level.bodies[player].solid = true;  // its own shape never gets in its way
    for (int i = 0; i < 180; ++i) level.frame(player, glm::vec3(0.0f));
    CHECK(near(level.poses[player].position.z, 1.0f));
}

static void testObjectsThatAreNotSolidOrHaveNoCollisionAreWalkedThrough() {
    {   // an Object set to not be solid (a pickup)
        Level level;
        level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
        level.add(glm::vec3(0, 5, 1), glm::vec3(10, 0.5f, 2), false);
        const size_t player = level.addCharacter(glm::vec3(0, 0, 1));
        for (int i = 0; i < 180; ++i) level.frame(player, glm::vec3(0, 5.0f / 60.0f, 0));
        CHECK(level.poses[player].position.y > 12.0f);
    }
    {   // a script that turned collision off with `make Collision.Enabled false`
        Level level;
        level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
        const size_t wall = level.add(glm::vec3(0, 5, 1), glm::vec3(10, 0.5f, 2));
        level.poses[wall].collisionEnabled = false;
        const size_t player = level.addCharacter(glm::vec3(0, 0, 1));
        for (int i = 0; i < 180; ++i) level.frame(player, glm::vec3(0, 5.0f / 60.0f, 0));
        CHECK(level.poses[player].position.y > 12.0f);
    }
    {   // a character that is itself a ghost, or has been destroyed, is left where its script put it
        Level level;
        level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
        const size_t player = level.addCharacter(glm::vec3(0, 0, 8));
        level.poses[player].collisionEnabled = false;
        const std::vector<size_t> moved = level.frame(player, glm::vec3(0, 1, 0));
        CHECK(moved.empty() && level.poses[player].position == glm::vec3(0, 1, 8));
    }
}

static void testJumpComesFromTheCharactersOwnAction() {
    // Which Input Action makes a character jump is its own setting, and the input is its own (its Object's Input Mapping).
    struct Case { const char* action; bool pressed; bool jumps; const char* why; };
    const Case cases[] = {
        {"jump", true, true, "the default action"},
        {"dash", true, false, "a different action is bound: `jump` doesn't make it jump"},
        {"", true, false, "no jump action: the button does nothing to it"},
        {"jump", false, false, "nothing pressed"},
    };
    for (const Case& c : cases) {
        Level level;
        level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
        const size_t player = level.addCharacter(glm::vec3(0, 0, 1));
        level.bodies[player].movement.jumpAction = c.action;
        for (int i = 0; i < 30; ++i) level.frame(player, glm::vec3(0.0f));
        level.frame(player, glm::vec3(0.0f), c.pressed);
        float apex = 0.0f;
        for (int i = 0; i < 60; ++i) {
            level.frame(player, glm::vec3(0.0f));
            apex = (std::max)(apex, level.poses[player].position.z);
        }
        if (c.jumps != (apex > 2.0f)) std::printf("  (case: %s)\n", c.why);
        CHECK(c.jumps == (apex > 2.0f));
    }

    // Two characters with their own input: only the one whose own `jump` went down jumps.
    Level level;
    level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
    const size_t a = level.addCharacter(glm::vec3(-5, 0, 1));
    const size_t b = level.addCharacter(glm::vec3(5, 0, 1));
    for (int i = 0; i < 30; ++i) level.frame(a, glm::vec3(0.0f));
    std::vector<size_t> moved;
    stepBodies(level.bodies, level.poses, 1.0f / 60.0f, [&](size_t body, const std::string& action) { return body == b && action == "jump"; }, &moved);
    for (int i = 0; i < 20; ++i) level.frame(a, glm::vec3(0.0f));
    CHECK(level.poses[b].position.z > 1.5f && near(level.poses[a].position.z, 1.0f, 0.02f));
}

static void testAFarJumpIsATeleportNotAWalk() {
    Level level;
    level.add(glm::vec3(0, 0, -0.5f), glm::vec3(100, 100, 0.5f));
    level.add(glm::vec3(0, 25, 1), glm::vec3(10, 0.5f, 2));  // a wall between here and there
    const size_t player = level.addCharacter(glm::vec3(0, 0, 1));
    level.frame(player, glm::vec3(0.0f));
    level.frame(player, glm::vec3(0, 50, 0));  // the script sets its location 50 away: it doesn't walk through the wall
    for (int i = 0; i < 60; ++i) level.frame(player, glm::vec3(0.0f));
    CHECK(near(level.poses[player].position.y, 50.0f));
    CHECK(near(level.poses[player].position.z, 1.0f));
}

static void testTheCapsuleScalesWithTheObject() {
    Level level;
    level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
    const size_t giant = level.addCharacter(glm::vec3(0, 0, 4), glm::vec3(2.0f));  // twice the size: a radius of 1, a half height of 2
    for (int i = 0; i < 180; ++i) level.frame(giant, glm::vec3(0.0f));
    CHECK(near(level.poses[giant].position.z, 2.0f));

    level.add(glm::vec3(0, 5, 2), glm::vec3(10, 0.5f, 4));  // a wall with its near face at y = 4.5
    for (int i = 0; i < 180; ++i) level.frame(giant, glm::vec3(0, 5.0f / 60.0f, 0));
    CHECK(near(level.poses[giant].position.y, 3.5f));        // stopped a full radius (1.0) short of it
}

static void testNothingHappensWithoutTime() {
    Level level;
    level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
    const size_t player = level.addCharacter(glm::vec3(0, 0, 5));
    std::vector<size_t> moved;
    stepBodies(level.bodies, level.poses, 0.0f, nullptr, &moved);  // the game is paused
    CHECK(moved.empty() && level.poses[player].position == glm::vec3(0, 0, 5));
    level.poses.pop_back();  // a scene that doesn't match its bodies is left alone
    stepBodies(level.bodies, level.poses, 0.016f, nullptr, &moved);
    CHECK(moved.empty());
}

static void testWalkingThroughAPickupTouchesItOnce() {
    Level level;
    level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
    const size_t coin = level.add(glm::vec3(0, 5, 1), glm::vec3(0.5f), false);  // a ball half as wide as the player, not solid
    const size_t player = level.addCharacter(glm::vec3(0, 0, 1));
    level.script(coin);
    level.script(player);
    for (int i = 0; i < 240; ++i) level.frame(player, glm::vec3(0, 5.0f / 60.0f, 0));  // walks right through it and out the other side
    CHECK(level.poses[player].position.y > 12.0f);
    CHECK(countOf(level.began, player, coin) == 1);
    CHECK(countOf(level.ended, player, coin) == 1);
    CHECK(countOf(level.hit, player, coin) == 0);    // it never stopped the player
    CHECK(level.began.size() == 1 && level.ended.size() == 1);
    CHECK(level.began[0].first < level.began[0].second);  // always (lower, higher), whichever way round it happened
    CHECK(level.contacts.overlapping.empty());
}

static void testTouchingStartsBeforeTheyOverlapByAnythingButAHair() {
    Level level;
    level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
    const size_t coin = level.add(glm::vec3(0, 5, 1), glm::vec3(0.5f), false);
    const size_t player = level.addCharacter(glm::vec3(0, 0, 1));
    level.script(coin);
    level.script(player);
    float firstTouch = -1.0f;
    for (int i = 0; i < 240 && firstTouch < 0.0f; ++i) {
        level.frame(player, glm::vec3(0, 5.0f / 60.0f, 0));
        if (!level.began.empty()) firstTouch = level.poses[player].position.y;
    }
    // The player's capsule is 0.5 wide and the coin's ball 0.5: they meet when the centers are 1.0 apart (4.0).
    CHECK(firstTouch > 3.9f && firstTouch < 4.1f);
}

static void testWithinAHairIsTouchingAndLongThinThingsAreNotMissed() {
    {   // a gap of 0.01 is resting against it; a gap of 0.05 is not
        for (float gap : {0.01f, 0.05f}) {
            Level level;
            const size_t a = level.add(glm::vec3(0, 0, 0), glm::vec3(1.0f), false);
            const size_t b = level.add(glm::vec3(2.0f + gap, 0, 0), glm::vec3(1.0f), false);
            level.script(a);
            level.script(b);
            level.frame(b, glm::vec3(0.0f));
            CHECK(countOf(level.began, a, b) == (gap < 0.02f ? 1 : 0));
        }
    }
    {   // two long planks overlapping at their ends: far apart as centers, but touching
        Level level;
        const size_t a = level.add(glm::vec3(0, 0, 0), glm::vec3(5, 0.1f, 0.1f), false);
        const size_t b = level.add(glm::vec3(9, 0, 0), glm::vec3(5, 0.1f, 0.1f), false);
        level.script(a);
        level.script(b);
        level.frame(b, glm::vec3(0.0f));
        CHECK(countOf(level.began, a, b) == 1);
    }
}

static void testBumpingIntoASolidScriptedObjectIsOneHit() {
    Level level;
    level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
    const size_t door = level.add(glm::vec3(0, 5, 1), glm::vec3(10, 0.5f, 2));  // a wall, with a script
    const size_t player = level.addCharacter(glm::vec3(0, 0, 1));
    level.script(door);
    level.script(player);
    for (int i = 0; i < 120; ++i) level.frame(player, glm::vec3(0, 5.0f / 60.0f, 0));  // walks into it and keeps pushing
    CHECK(countOf(level.hit, player, door) == 1);  // one hit, not one for every frame it is pressed against it
    CHECK(near(level.poses[player].position.y, 4.0f));

    for (int i = 0; i < 60; ++i) level.frame(player, glm::vec3(0, -5.0f / 60.0f, 0));  // backs away
    for (int i = 0; i < 120; ++i) level.frame(player, glm::vec3(0, 5.0f / 60.0f, 0));  // and bumps again
    CHECK(countOf(level.hit, player, door) == 2);
}

static void testOnlyScriptedObjectsReportContacts() {
    Level level;
    const size_t floorIndex = level.add(glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));
    const size_t wall = level.add(glm::vec3(0, 5, 1), glm::vec3(10, 0.5f, 2));
    const size_t player = level.addCharacter(glm::vec3(0, 0, 3));
    level.script(player);                       // the floor and the wall have no script to tell
    for (int i = 0; i < 180; ++i) level.frame(player, glm::vec3(0, 5.0f / 60.0f, 0));
    CHECK(near(level.poses[player].position.y, 4.0f));  // they still stop it
    CHECK(level.began.empty() && level.ended.empty() && level.hit.empty());
    (void)floorIndex;
    (void)wall;
}

static void testStandingOnAScriptedPlatformTouchesItUntilYouLeave() {
    Level level;
    const size_t platform = level.add(glm::vec3(0, 0, -0.5f), glm::vec3(10, 10, 0.5f));  // top at z = 0
    level.add(glm::vec3(0, 0, -20.5f), glm::vec3(50, 50, 0.5f));                           // a far floor, so it can come down again
    const size_t player = level.addCharacter(glm::vec3(0, 0, 3));
    level.script(platform);
    level.script(player);
    for (int i = 0; i < 120; ++i) level.frame(player, glm::vec3(0.0f));                   // drops onto it and stands
    CHECK(countOf(level.began, player, platform) == 1 && countOf(level.ended, player, platform) == 0);
    CHECK(level.hit.size() == 1);                                                          // landing on it is a hit, once
    for (int i = 0; i < 60; ++i) level.frame(player, glm::vec3(0.0f));                    // standing still adds nothing
    CHECK(countOf(level.began, player, platform) == 1 && level.hit.size() == 1);

    level.frame(player, glm::vec3(0.0f), true);                                           // jumps
    for (int i = 0; i < 20; ++i) level.frame(player, glm::vec3(0.0f));
    CHECK(countOf(level.ended, player, platform) == 1);                                    // in the air: no longer touching
    for (int i = 0; i < 120; ++i) level.frame(player, glm::vec3(0.0f));                   // and lands again
    CHECK(countOf(level.began, player, platform) == 2);
}

static void testScriptedObjectsThatDontMoveByThemselvesStillTouch() {
    // Neither is a character: a door that swings shut (its script moves it) across a coin.
    Level level;
    const size_t coin = level.add(glm::vec3(0, 0, 0), glm::vec3(0.5f), false);
    const size_t door = level.add(glm::vec3(5, 0, 0), glm::vec3(0.5f, 2, 2), false);
    level.script(coin);
    level.script(door);
    level.frame(door, glm::vec3(0.0f));
    CHECK(level.began.empty());
    for (int i = 0; i < 10; ++i) level.frame(door, glm::vec3(-0.5f, 0, 0));  // the script slides it along
    CHECK(countOf(level.began, coin, door) == 1);
    for (int i = 0; i < 20; ++i) level.frame(door, glm::vec3(-0.5f, 0, 0));  // and out the other side
    CHECK(countOf(level.ended, coin, door) == 1);
}

static void testTurningCollisionOffEndsTheTouch() {
    Level level;
    const size_t coin = level.add(glm::vec3(0, 0, 0), glm::vec3(1.0f), false);
    const size_t other = level.add(glm::vec3(0.5f, 0, 0), glm::vec3(1.0f), false);
    level.script(coin);
    level.script(other);
    level.frame(other, glm::vec3(0.0f));
    CHECK(countOf(level.began, coin, other) == 1);
    level.poses[coin].collisionEnabled = false;                               // `make Collision.Enabled false`, or `destroy Self`
    level.frame(other, glm::vec3(0.0f));
    CHECK(countOf(level.ended, coin, other) == 1 && level.contacts.overlapping.empty());
    level.poses[coin].collisionEnabled = true;
    level.frame(other, glm::vec3(0.0f));
    CHECK(countOf(level.began, coin, other) == 2);
}

int main() {
    testWalkingThroughAPickupTouchesItOnce();
    testTouchingStartsBeforeTheyOverlapByAnythingButAHair();
    testWithinAHairIsTouchingAndLongThinThingsAreNotMissed();
    testBumpingIntoASolidScriptedObjectIsOneHit();
    testOnlyScriptedObjectsReportContacts();
    testStandingOnAScriptedPlatformTouchesItUntilYouLeave();
    testScriptedObjectsThatDontMoveByThemselvesStillTouch();
    testTurningCollisionOffEndsTheTouch();
    testSignedDistance();

    testTraces();
    testOverlaps();
    testCapsulesAsObstacles();
    testAScriptedCharacterStopsAtASolidWall();
    testOnlyCharactersAreMoved();
    testCharactersFallOnTheirOwn();
    testObjectsThatAreNotSolidOrHaveNoCollisionAreWalkedThrough();
    testJumpComesFromTheCharactersOwnAction();
    testAFarJumpIsATeleportNotAWalk();
    testTheCapsuleScalesWithTheObject();
    testNothingHappensWithoutTime();
    testFallsOntoTheFloorAndStaysThere();
    testRestingHeightDoesNotDependOnTheFrameRate();
    testWalkingStaysOnTheGround();
    testAWallStopsTheCapsule();
    testSlidingAlongATurnedWall();
    testWalkingOffAnEdgeFalls();
    testJumpGoesUpAndComesBackDown();
    testJumpOnlyFromTheGround();
    testACeilingStopsAJump();
    testAThinWallStopsAFastCapsule();
    testTheWholeBodyCounts();
    testARampIsWalkable();
    testASteepSlopeIsAWall();
    testSphereAndCylinderShapes();
    testShapesComeFromMeshBounds();
    testPushOutOfShape();
    testEmptyWorldAndFastFall();
    testASlimCapsuleIsOneBall();

    if (failures == 0) {
        std::printf("ALL PASSED\n");
        return 0;
    }
    std::printf("%d FAILED\n", failures);
    return 1;
}
