// Tests for the collision wireframe (include/physics/CollisionOutline.h): every drawn point lies on the physics shape.
//
//   cmake --build . --target CollisionOutlineTest --config Debug
//   build/tests/CollisionOutlineTest.exe

#include "physics/CollisionOutline.h"

#include <cmath>
#include <cstdio>

using namespace Propulsion;
using namespace Propulsion::Physics;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static glm::quat turned() { return glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f))); }

// The outline is the shape's surface, so each end of each segment is (within rounding) on it, and so is the middle of
// any segment that lies along a flat face or a straight side.
static bool onSurface(const Shape& s, const std::vector<Segment>& outline, float eps = 1e-3f) {
    for (const Segment& seg : outline) {
        if (std::fabs(signedDistance(s, seg.first)) > eps || std::fabs(signedDistance(s, seg.second)) > eps) return false;
    }
    return true;
}

static void testBox() {
    Shape s = shapeFromMesh(ShapeKind::Box, glm::vec3(-0.5f), glm::vec3(0.5f), glm::vec3(1.0f, 2.0f, 3.0f), glm::quat(1, 0, 0, 0),
                            glm::vec3(2.0f, 4.0f, 6.0f));
    const std::vector<Segment> outline = shapeOutline(s);
    CHECK(outline.size() == 12);
    CHECK(onSurface(s, outline));
    for (const Segment& seg : outline) {  // an edge runs along one axis: it's as long as a side
        const float len = glm::distance(seg.first, seg.second);
        CHECK(std::fabs(len - 2.0f) < 1e-4f || std::fabs(len - 4.0f) < 1e-4f || std::fabs(len - 6.0f) < 1e-4f);
    }

    s.rotation = turned();  // turned: the edges are still on the surface and the same length
    const std::vector<Segment> rotated = shapeOutline(s);
    CHECK(rotated.size() == 12);
    CHECK(onSurface(s, rotated));
}

static void testOffCenterBounds() {
    // A mesh whose bounds aren't centered on its origin: the box is where the mesh is, not at the object's position.
    Shape s = shapeFromMesh(ShapeKind::Box, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 2.0f), glm::vec3(10.0f, 0.0f, 0.0f),
                            glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    const std::vector<Segment> outline = shapeOutline(s);
    glm::vec3 low(1e9f), high(-1e9f);
    for (const Segment& seg : outline) {
        low = glm::min(low, glm::min(seg.first, seg.second));
        high = glm::max(high, glm::max(seg.first, seg.second));
    }
    CHECK(glm::distance(low, glm::vec3(10.0f, 0.0f, 0.0f)) < 1e-4f);
    CHECK(glm::distance(high, glm::vec3(11.0f, 1.0f, 2.0f)) < 1e-4f);
}

static void testSphere() {
    Shape s = shapeFromMesh(ShapeKind::Sphere, glm::vec3(-0.5f), glm::vec3(0.5f), glm::vec3(-2.0f, 1.0f, 0.5f), turned(), glm::vec3(3.0f));
    const std::vector<Segment> outline = shapeOutline(s);
    CHECK(outline.size() == 3 * kOutlineCircleSegments);
    CHECK(onSurface(s, outline));
}

static void testCylinder() {
    // A cylinder mesh runs along its own Y axis, as drawn.
    Shape s = shapeFromMesh(ShapeKind::Cylinder, glm::vec3(-0.5f), glm::vec3(0.5f), glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(2.0f, 6.0f, 2.0f));
    const std::vector<Segment> outline = shapeOutline(s);
    CHECK(outline.size() == 2 * kOutlineCircleSegments + 4);
    CHECK(onSurface(s, outline));
    float lowY = 1e9f, highY = -1e9f;
    for (const Segment& seg : outline) {
        lowY = (std::min)(lowY, (std::min)(seg.first.y, seg.second.y));
        highY = (std::max)(highY, (std::max)(seg.first.y, seg.second.y));
    }
    CHECK(std::fabs(lowY + 3.0f) < 1e-4f && std::fabs(highY - 3.0f) < 1e-4f);  // its length is along Y

    s.rotation = turned();
    CHECK(onSurface(s, shapeOutline(s)));
}

static void testCapsule() {
    Shape s = capsuleShape(glm::vec3(1.0f, 2.0f, 3.0f), 0.5f, 1.0f);  // upright, 2 tall, 1 wide
    const std::vector<Segment> outline = shapeOutline(s);
    CHECK(onSurface(s, outline));
    float lowZ = 1e9f, highZ = -1e9f, widest = 0.0f;
    for (const Segment& seg : outline) {
        for (const glm::vec3& p : {seg.first, seg.second}) {
            lowZ = (std::min)(lowZ, p.z);
            highZ = (std::max)(highZ, p.z);
            widest = (std::max)(widest, glm::length(glm::vec2(p.x - 1.0f, p.y - 2.0f)));
        }
    }
    CHECK(std::fabs(lowZ - 2.0f) < 1e-4f && std::fabs(highZ - 4.0f) < 1e-4f);  // 1 below and 1 above its center, to the tips of the caps
    CHECK(std::fabs(widest - 0.5f) < 1e-4f);

    // A capsule as wide as it is tall is a ball: no straight part, and still on its surface.
    Shape ball = capsuleShape(glm::vec3(0.0f), 1.0f, 1.0f);
    CHECK(onSurface(ball, shapeOutline(ball)));
}

static void testNothingBroken() {
    // A shape with a zero or silly size still gives finite points (the overlay must never feed NaN to the renderer).
    Shape s;
    s.kind = ShapeKind::Box;
    s.half = glm::vec3(0.0f);
    for (const Segment& seg : shapeOutline(s)) {
        CHECK(std::isfinite(seg.first.x) && std::isfinite(seg.second.z));
    }
}

int main() {
    testBox();
    testOffCenterBounds();
    testSphere();
    testCylinder();
    testCapsule();
    testNothingBroken();
    if (failures == 0) std::printf("CollisionOutlineTest: all passed\n");
    else std::printf("CollisionOutlineTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
