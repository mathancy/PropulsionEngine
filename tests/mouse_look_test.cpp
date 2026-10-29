// Tests for turning mouse movement into the lookX / lookY axes (include/scripting/MouseLook.h).
//
//   cmake --build . --target MouseLookTest --config Debug
//   build/tests/MouseLookTest.exe

#include "scripting/MouseLook.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

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

static void testFirstReadingIsOnlyAStartingPoint() {
    MouseLook look;
    look.update(640.0, 360.0, 1.0f / 60.0f);
    CHECK(look.x == 0.0f && look.y == 0.0f);  // no movement yet, however far from the origin the cursor is
}

static void testDirections() {
    MouseLook look;
    look.update(100.0, 100.0, 0.1f);
    look.update(150.0, 100.0, 0.1f);  // 50 pixels right in 0.1 s = 500 per second
    CHECK(near(look.x, 0.5f) && near(look.y, 0.0f));  // right is positive
    look.update(150.0, 80.0, 0.1f);   // 20 pixels up the screen (y gets smaller) in 0.1 s
    CHECK(near(look.x, 0.0f) && near(look.y, 0.2f));  // up is positive
    look.update(130.0, 80.0, 0.1f);   // left
    CHECK(near(look.x, -0.2f));
    look.update(130.0, 100.0, 0.1f);  // down
    CHECK(near(look.y, -0.2f));
}

static void testStillMouseReadsZero() {
    MouseLook look;
    look.update(10.0, 10.0, 0.016f);
    look.update(30.0, 10.0, 0.016f);
    CHECK(look.x > 0.0f);
    look.update(30.0, 10.0, 0.016f);  // the next frame it didn't move
    CHECK(look.x == 0.0f && look.y == 0.0f);
}

// The scripts multiply an axis by `per second` (the frame's time), so the same hand movement must turn the same
// amount whatever the frame rate: the sum of axis * time is the pixels moved, over 1000.
static void testTheSameFlickTurnsTheSameAtAnyFrameRate() {
    for (float fps : {30.0f, 60.0f, 144.0f, 240.0f}) {
        MouseLook look;
        const float dt = 1.0f / fps;
        const int frames = static_cast<int>(fps / 2);  // half a second of movement
        double cursor = 500.0;
        look.update(cursor, 0.0, dt);
        float turned = 0.0f;
        for (int i = 0; i < frames; i++) {
            cursor += 800.0 / frames;  // 800 pixels over the half second
            look.update(cursor, 0.0, dt);
            turned += look.x * dt;
        }
        CHECK(near(turned, 0.8f, 1e-3f));
    }
}

static void testFasterThanAStickIsAllowed() {
    MouseLook look;
    look.update(0.0, 0.0, 0.01f);
    look.update(100.0, 0.0, 0.01f);  // 10000 pixels per second
    CHECK(near(look.x, 10.0f));      // a flick isn't capped at 1
}

static void testResetForgetsTheOldPosition() {
    // The cursor was captured: it jumps from wherever it was in the editor to the middle of the window.
    MouseLook look;
    look.update(100.0, 100.0, 0.016f);
    look.update(120.0, 100.0, 0.016f);
    CHECK(look.x > 0.0f);
    look.reset();
    CHECK(look.x == 0.0f && look.y == 0.0f);
    look.update(900.0, 700.0, 0.016f);  // not a 780 pixel flick
    CHECK(look.x == 0.0f && look.y == 0.0f);
    look.update(910.0, 700.0, 0.016f);  // the movement after that is counted again
    CHECK(look.x > 0.0f);
}

static void testNoTimePassingDoesNotDivideByZero() {
    MouseLook look;
    look.update(0.0, 0.0, 0.016f);
    look.update(50.0, 50.0, 0.0f);
    CHECK(std::isfinite(look.x) && std::isfinite(look.y));
    CHECK(look.x == 0.0f && look.y == 0.0f);
}

int main() {
    testFirstReadingIsOnlyAStartingPoint();
    testDirections();
    testStillMouseReadsZero();
    testTheSameFlickTurnsTheSameAtAnyFrameRate();
    testFasterThanAStickIsAllowed();
    testResetForgetsTheOldPosition();
    testNoTimePassingDoesNotDivideByZero();
    if (failures == 0) std::printf("MouseLookTest: all passed\n");
    else std::printf("MouseLookTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
