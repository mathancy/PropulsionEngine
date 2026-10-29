// Tests for the shadow geometry (include/rendering/ShadowMath.h) and how lights are given shadows
// (include/rendering/LightingData.h): where the sun's map goes, that it holds still as the camera moves, and that a point
// light's six faces cover every direction. No window or GPU.
//
//   cmake --build . --target ShadowTest --config Debug
//   build/tests/ShadowTest.exe

#include "rendering/LightingData.h"
#include "rendering/ShadowMath.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace Propulsion;
using namespace Propulsion::Shadows;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
static bool nearVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

// The engine's camera: GL-style perspective with Y flipped for Vulkan, Z up.
static glm::mat4 engineProjection(float fovDegrees, float aspect, float nearPlane = 0.1f, float farPlane = 1000.0f) {
    glm::mat4 p = glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
    p[1][1] *= -1.0f;
    return p;
}
static glm::mat4 viewFrom(const glm::vec3& eye, const glm::vec3& target) { return glm::lookAt(eye, target, glm::vec3(0, 0, 1)); }

static glm::vec3 ndc(const glm::mat4& m, const glm::vec3& p) {
    const glm::vec4 c = m * glm::vec4(p, 1.0f);
    return glm::vec3(c) / c.w;
}

// ---- Frustum slice -----------------------------------------------------------------------------------------------------

static void testFrustumSliceCorners() {
    const glm::vec3 eye(0, -5, 2);
    const glm::mat4 view = viewFrom(eye, glm::vec3(0, 5, 2));  // looking along +Y
    const glm::mat4 projection = engineProjection(60.0f, 1.5f);
    const auto corners = frustumSliceCorners(view, projection, 1.0f, 20.0f);

    const glm::vec3 forward(0, 1, 0);
    for (int i = 0; i < 8; i++) {
        const float distance = glm::dot(corners[i] - eye, forward);
        CHECK(near(distance, i < 4 ? 1.0f : 20.0f, 1e-3f));
    }
    // Half the field of view is 30 degrees; the width follows from the aspect ratio.
    const float halfHeightFar = 20.0f * std::tan(glm::radians(30.0f));
    for (int i = 4; i < 8; i++) {
        CHECK(near(std::fabs(corners[i].z - eye.z), halfHeightFar, 1e-2f));
        CHECK(near(std::fabs(corners[i].x - eye.x), halfHeightFar * 1.5f, 1e-2f));
    }
}

// ---- The sun's shadow map ----------------------------------------------------------------------------------------------

static std::array<glm::vec3, 8> sliceFor(const glm::vec3& eye, const glm::vec3& target, float farDistance = 60.0f) {
    return frustumSliceCorners(viewFrom(eye, target), engineProjection(60.0f, 1.6f), 0.1f, farDistance);
}

static void testSunMapCoversTheView() {
    const auto corners = sliceFor(glm::vec3(6, 6, 4), glm::vec3(0, 0, 0));
    const glm::vec3 sunTravel = glm::normalize(glm::vec3(-0.4f, 0.5f, -1.0f));
    const DirectionalShadow shadow = fitDirectionalShadow(corners, sunTravel, Aabb(), 2048);
    CHECK(shadow.valid);
    CHECK(shadow.depthRange > 0.0f && shadow.orthoWidth > 0.0f);
    for (const glm::vec3& c : corners) {
        const glm::vec3 n = ndc(shadow.viewProjection, c);
        CHECK(n.x >= -1.0001f && n.x <= 1.0001f);
        CHECK(n.y >= -1.0001f && n.y <= 1.0001f);
        CHECK(n.z >= -0.0001f && n.z <= 1.0001f);
    }
}

static void testSunMapReachesCastersTowardTheSun() {
    // Something high above the view, along the sun's direction, still casts into it: the map must reach back to it.
    const auto corners = sliceFor(glm::vec3(6, 6, 4), glm::vec3(0, 0, 0));
    Aabb casters;
    casters.expand(glm::vec3(-1, -1, 0));
    casters.expand(glm::vec3(1, 1, 40));  // a tall tower
    const DirectionalShadow shadow = fitDirectionalShadow(corners, glm::vec3(0, 0, -1), casters, 2048);
    const glm::vec3 top = ndc(shadow.viewProjection, glm::vec3(0, 0, 40));
    CHECK(top.z >= 0.0f && top.z <= 1.0f);
    const glm::vec3 ground = ndc(shadow.viewProjection, glm::vec3(0, 0, 0));
    CHECK(top.z < ground.z);  // nearer the sun than the ground is

    // Without it, the tower is outside the map's depth range (for a view that doesn't reach that high).
    const auto nearView = sliceFor(glm::vec3(6, 6, 4), glm::vec3(0, 0, 0), 14.0f);
    const DirectionalShadow without = fitDirectionalShadow(nearView, glm::vec3(0, 0, -1), Aabb(), 2048);
    CHECK(ndc(without.viewProjection, glm::vec3(0, 0, 40)).z < 0.0f);
}

static void testSunMapHoldsStill() {
    // A fixed point on the ground lands on the same fraction of a texel however the camera moves, so a shadow edge
    // doesn't crawl or shimmer.
    const int size = 2048;
    const glm::vec3 sunTravel = glm::normalize(glm::vec3(-0.4f, 0.5f, -1.0f));
    const glm::vec3 point(1.3f, -0.7f, 0.0f);
    auto texel = [&](const glm::vec3& eye, const glm::vec3& target) {
        const DirectionalShadow shadow = fitDirectionalShadow(sliceFor(eye, target), sunTravel, Aabb(), size);
        const glm::vec3 n = ndc(shadow.viewProjection, point);
        return glm::vec2((n.x * 0.5f + 0.5f) * size, (n.y * 0.5f + 0.5f) * size);
    };
    const glm::vec2 base = texel(glm::vec3(6, 6, 4), glm::vec3(0, 0, 0));
    for (const glm::vec3& shift : {glm::vec3(0.013f, 0, 0), glm::vec3(0.1f, -0.2f, 0), glm::vec3(0.37f, 0.5f, 0.1f), glm::vec3(3.1f, 1.7f, 0.0f), glm::vec3(5.77f, -4.2f, 0.3f)}) {
        const glm::vec2 moved = texel(glm::vec3(6, 6, 4) + shift, glm::vec3(0, 0, 0) + shift);
        // Compare the fractions of a texel (a camera move may carry the point a whole number of texels).
        const float fx = (moved.x - base.x) - std::round(moved.x - base.x);
        const float fy = (moved.y - base.y) - std::round(moved.y - base.y);
        CHECK(std::fabs(fx) < 0.02f);
        CHECK(std::fabs(fy) < 0.02f);
    }

    // Turning the camera doesn't change the size of the map either.
    const DirectionalShadow a = fitDirectionalShadow(sliceFor(glm::vec3(0, -10, 4), glm::vec3(0, 0, 2)), sunTravel, Aabb(), size);
    const DirectionalShadow b = fitDirectionalShadow(sliceFor(glm::vec3(0, -10, 4), glm::vec3(8, -4, 2)), sunTravel, Aabb(), size);
    CHECK(near(a.orthoWidth, b.orthoWidth, 1e-2f));
    CHECK(near(a.depthRange, a.depthRange));
}

static void testSunMapStraightDownAndBadInput() {
    // A sun straight down and one straight up (the "up" vector can't be the same as the way it shines).
    const auto corners = sliceFor(glm::vec3(6, 6, 4), glm::vec3(0, 0, 0));
    for (float z : {-1.0f, 1.0f}) {
        const DirectionalShadow shadow = fitDirectionalShadow(corners, glm::vec3(0, 0, z), Aabb(), 1024);
        CHECK(shadow.valid);
        const glm::vec3 n = ndc(shadow.viewProjection, glm::vec3(0, 0, 0));
        CHECK(n.x > -1.0f && n.x < 1.0f && n.y > -1.0f && n.y < 1.0f);
    }
    // No direction, no map.
    CHECK(!fitDirectionalShadow(corners, glm::vec3(0.0f), Aabb(), 1024).valid);
    CHECK(!fitDirectionalShadow(corners, glm::vec3(0, 0, -1), Aabb(), 0).valid);
}

static void testAabb() {
    Aabb box;
    CHECK(!box.valid());
    box.expand(glm::vec3(1, 2, 3));
    box.expand(glm::vec3(-1, 5, 0));
    CHECK(box.valid() && box.min == glm::vec3(-1, 2, 0) && box.max == glm::vec3(1, 5, 3));
    Aabb other;
    other.expand(glm::vec3(10, 0, 0));
    box.expand(other);
    CHECK(box.max.x == 10.0f);
    box.expand(Aabb());  // an empty box adds nothing
    CHECK(box.max.x == 10.0f && box.min.x == -1.0f);

    // A unit cube turned a quarter about Z and moved: still a unit cube, in its new place.
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(5, 0, 0)) * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));
    const Aabb moved = Aabb::transformed(glm::vec3(-0.5f), glm::vec3(0.5f), model);
    CHECK(nearVec(moved.min, glm::vec3(4.5f, -0.5f, -0.5f)) && nearVec(moved.max, glm::vec3(5.5f, 0.5f, 0.5f)));
}

// ---- Cascades ----------------------------------------------------------------------------------------------------------

static void testCascadeSplits() {
    for (float distance : {1.0f, 12.0f, 100.0f, 1000.0f, 5000.0f}) {
        const auto splits = cascadeSplits(0.1f, distance);
        CHECK(splits[kCascadeCount - 1] == distance);
        float previous = 0.1f;
        for (int i = 0; i < kCascadeCount; i++) {
            CHECK(splits[i] > previous);  // each map reaches farther than the one before
            previous = splits[i];
        }
        // The nearest map is a small part of the whole (that is what makes it sharp), and the ones after grow by a steady factor.
        if (distance >= 12.0f) CHECK(splits[0] < 0.5f * distance && splits[0] < 12.0f);
        if (distance >= 100.0f) CHECK(splits[0] < 0.1f * distance);
        for (int i = 2; i < kCascadeCount; i++) CHECK(near((splits[i] / splits[i - 1]) / (splits[i - 1] / splits[i - 2]), 1.0f, 0.01f));
        for (int i = 2; i < kCascadeCount; i++) CHECK(splits[i] - splits[i - 1] > splits[i - 1] - splits[i - 2] - 1e-3f);  // each covers more
        for (int i = 0; i < kCascadeCount; i++) CHECK(cascadeBlendWidth(splits, i) > 0.0f && cascadeBlendWidth(splits, i) < splits[i]);
    }
    // Nonsense distances still give increasing splits.
    const auto tiny = cascadeSplits(0.1f, 0.05f);
    for (int i = 1; i < kCascadeCount; i++) CHECK(tiny[i] >= tiny[i - 1]);
}

// A place in the view at a given depth along the view direction, and sideways fractions of the way to the screen's edge.
static glm::vec3 viewPoint(const glm::mat4& view, const glm::mat4& projection, float depth, float sx, float sy) {
    const auto corners = frustumSliceCorners(view, projection, depth, depth * 2.0f);  // the slice at that depth: corners 0..3
    const glm::vec3 center = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
    return center + (corners[1] - corners[0]) * 0.5f * sx + (corners[2] - corners[0]) * 0.5f * sy;
}

static void testCascadesCoverTheView() {
    const glm::mat4 view = viewFrom(glm::vec3(6, 6, 4), glm::vec3(0, 0, 0));
    const glm::mat4 projection = engineProjection(60.0f, 1.6f);
    const glm::vec3 sunTravel = glm::normalize(glm::vec3(-0.4f, 0.5f, -1.0f));
    const glm::vec3 forward = glm::normalize(glm::vec3(-6, -6, -4));

    for (float distance : {30.0f, 100.0f, 400.0f}) {
        const CascadedShadow shadow = fitCascadedShadow(view, projection, 0.1f, distance, sunTravel, Aabb(), kCascadeMapSize);
        CHECK(shadow.valid && shadow.count == kCascadeCount);

        // Anywhere in the view the shader would look in a map (the nearest that reaches that far), the point is well inside it.
        // Within a fade zone, the next map must cover it too.
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const float inside = 1.0f - 2.0f * 0.03f;  // the shader's margin
        for (int i = 0; i < 4000; i++) {
            const float depth = 0.1f + unit(rng) * (distance - 0.1f);
            const glm::vec3 p = viewPoint(view, projection, depth, unit(rng) * 2.0f - 1.0f, unit(rng) * 2.0f - 1.0f);
            CHECK(near(glm::dot(p - glm::vec3(6, 6, 4), forward), depth, 0.05f));

            int c = 0;
            while (c < kCascadeCount - 1 && depth > shadow.splitDepth[c]) c++;
            const glm::vec3 n = ndc(shadow.maps[c].viewProjection, p);
            CHECK(std::fabs(n.x) <= inside && std::fabs(n.y) <= inside && n.z >= -1e-4f && n.z <= 1.0001f);

            if (c < kCascadeCount - 1) {
                const float blendStart = shadow.splitDepth[c] - cascadeBlendWidth(shadow.splitDepth, c);
                if (depth > blendStart) {
                    const glm::vec3 m = ndc(shadow.maps[c + 1].viewProjection, p);
                    CHECK(std::fabs(m.x) <= inside && std::fabs(m.y) <= inside && m.z >= -1e-4f && m.z <= 1.0001f);
                }
            }
        }
    }
}

static void testNearCascadesAreSharper() {
    const glm::mat4 view = viewFrom(glm::vec3(6, 6, 4), glm::vec3(0, 0, 0));
    const glm::mat4 projection = engineProjection(60.0f, 1.6f);
    const glm::vec3 sunTravel = glm::normalize(glm::vec3(-0.4f, 0.5f, -1.0f));
    const CascadedShadow shadow = fitCascadedShadow(view, projection, 0.1f, 100.0f, sunTravel, Aabb(), kCascadeMapSize);
    // How much of the world one texel is: it grows with each map.
    float previous = 0.0f;
    for (int i = 0; i < kCascadeCount; i++) {
        const float texel = shadow.maps[i].orthoWidth / static_cast<float>(kCascadeMapSize);
        CHECK(texel > previous);
        previous = texel;
    }
    // Close to the camera a texel is far smaller than one map covering the whole distance would give, at the same memory
    // (four maps of 2048 squared are one of 4096 squared).
    const float oneMapTexel = fitDirectionalShadow(frustumSliceCorners(view, projection, 0.1f, 100.0f), sunTravel, Aabb(), kCascadeMapSize * 2).orthoWidth /
                              static_cast<float>(kCascadeMapSize * 2);
    CHECK(shadow.maps[0].orthoWidth / static_cast<float>(kCascadeMapSize) < 0.2f * oneMapTexel);
}

static void testCascadesHoldStill() {
    // Every map snaps to its own texels, so a shadow edge on the ground doesn't crawl as the camera moves (in any map).
    const glm::vec3 sunTravel = glm::normalize(glm::vec3(-0.4f, 0.5f, -1.0f));
    const glm::vec3 point(1.3f, -0.7f, 0.0f);
    auto texels = [&](const glm::vec3& eye, const glm::vec3& target) {
        const CascadedShadow shadow = fitCascadedShadow(viewFrom(eye, target), engineProjection(60.0f, 1.6f), 0.1f, 80.0f, sunTravel, Aabb(), kCascadeMapSize);
        std::array<glm::vec2, kCascadeCount> out{};
        for (int c = 0; c < kCascadeCount; c++) {
            const glm::vec3 n = ndc(shadow.maps[c].viewProjection, point);
            out[c] = glm::vec2((n.x * 0.5f + 0.5f) * kCascadeMapSize, (n.y * 0.5f + 0.5f) * kCascadeMapSize);
        }
        return out;
    };
    const auto base = texels(glm::vec3(6, 6, 4), glm::vec3(0, 0, 0));
    for (const glm::vec3& shift : {glm::vec3(0.013f, 0, 0), glm::vec3(0.37f, 0.5f, 0.1f), glm::vec3(5.77f, -4.2f, 0.3f)}) {
        const auto moved = texels(glm::vec3(6, 6, 4) + shift, glm::vec3(0, 0, 0) + shift);
        for (int c = 0; c < kCascadeCount; c++) {
            const float fx = (moved[c].x - base[c].x) - std::round(moved[c].x - base[c].x);
            const float fy = (moved[c].y - base[c].y) - std::round(moved[c].y - base[c].y);
            CHECK(std::fabs(fx) < 0.02f && std::fabs(fy) < 0.02f);
        }
    }
}

static void testSphereOverlapsMap() {
    const auto corners = sliceFor(glm::vec3(6, 6, 4), glm::vec3(0, 0, 0), 10.0f);
    const DirectionalShadow map = fitDirectionalShadow(corners, glm::normalize(glm::vec3(-0.4f, 0.5f, -1.0f)), Aabb(), 2048);
    glm::vec3 center(0.0f);
    for (const glm::vec3& c : corners) center += c / 8.0f;
    CHECK(sphereOverlapsMap(map, center, 0.1f));
    // Far off to the side, nothing; but something big enough to reach in does overlap.
    const glm::vec3 side = center + glm::vec3(1, 1, 0) * (map.orthoWidth * 2.0f);
    CHECK(!sphereOverlapsMap(map, side, 0.5f));
    CHECK(sphereOverlapsMap(map, side, map.orthoWidth * 4.0f));
    const glm::vec4 clipCenter = map.viewProjection * glm::vec4(center, 1.0f);
    CHECK(near(clipCenter.w, 1.0f, 1e-4f));  // orthographic
    CHECK(!sphereOverlapsMap(DirectionalShadow(), center, 1.0f));
}

// ---- Point lights ------------------------------------------------------------------------------------------------------

static void testPointFaceLooksAlongItsAxis() {
    const glm::vec3 light(3, -2, 5);
    const float nearPlane = 0.05f, farPlane = 20.0f;
    const auto faces = pointShadowFaces(light, nearPlane, farPlane);
    for (int f = 0; f < 6; f++) {
        for (float d : {0.5f, 3.0f, 15.0f}) {
            const glm::vec4 clip = faces[f] * glm::vec4(light + pointFaceDirection(f) * d, 1.0f);
            CHECK(near(clip.x / clip.w, 0.0f, 1e-4f) && near(clip.y / clip.w, 0.0f, 1e-4f));  // straight ahead is the middle
            CHECK(near(clip.w, d, 1e-3f));                                                    // w is the distance along the axis
            CHECK(near(linearizePointDepth(clip.z / clip.w, nearPlane, farPlane), d, d * 1e-3f));
        }
        const glm::vec3 atNear = ndc(faces[f], light + pointFaceDirection(f) * nearPlane);
        const glm::vec3 atFar = ndc(faces[f], light + pointFaceDirection(f) * farPlane);
        CHECK(near(atNear.z, 0.0f, 1e-3f) && near(atFar.z, 1.0f, 1e-3f));
    }
}

static void testEveryDirectionIsInItsFace() {
    const glm::vec3 light(-1, 4, 2);
    const auto faces = pointShadowFaces(light, 0.05f, 30.0f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    int used[6] = {};
    for (int i = 0; i < 2000; i++) {
        glm::vec3 d(dist(rng), dist(rng), dist(rng));
        if (glm::length(d) < 0.05f) continue;
        d = glm::normalize(d);
        const int face = pointFaceOf(d);
        used[face]++;
        const glm::vec4 clip = faces[face] * glm::vec4(light + d * 4.0f, 1.0f);
        CHECK(clip.w > 0.0f);
        CHECK(std::fabs(clip.x / clip.w) <= 1.0001f && std::fabs(clip.y / clip.w) <= 1.0001f);
    }
    for (int f = 0; f < 6; f++) CHECK(used[f] > 100);  // all six get used

    // The axes themselves, and a tie (the shader and C++ agree on which face wins).
    for (int f = 0; f < 6; f++) CHECK(pointFaceOf(pointFaceDirection(f)) == f);
    CHECK(pointFaceOf(glm::vec3(1, 1, 1)) == 0);
    CHECK(pointFaceOf(glm::vec3(0, 1, 1)) == 2);
}

// ---- Giving lights shadows ---------------------------------------------------------------------------------------------

static SceneLight sunLight(const glm::vec3& travel, bool casts = true) {
    SceneLight l;
    l.settings = LightSettings::directional();
    l.settings.castShadows = casts;
    l.direction = travel;
    return l;
}
static SceneLight bulbLight(const glm::vec3& position, bool casts = true) {
    SceneLight l;
    l.settings = LightSettings::point();
    l.settings.castShadows = casts;
    l.position = position;
    return l;
}

static void testShadowSlots() {
    EnvironmentLighting env;

    // The first sun and the first few bulbs get slots; later ones (and ones that don't cast) don't.
    std::vector<SceneLight> lights = {sunLight(glm::vec3(0, 0, -1)), sunLight(glm::vec3(1, 0, -1)), bulbLight(glm::vec3(0, 0, 3)),
                                      bulbLight(glm::vec3(5, 0, 3), false), bulbLight(glm::vec3(0, 5, 3))};
    ShadowAssignment shadows;
    GpuLighting gpu = buildLighting(lights, env, true, nullptr, &shadows);
    CHECK(gpu.envParams.w == 5.0f && shadows.lights.size() == 5);
    CHECK(shadows.directional == 0 && gpu.lights[0].params.y == 0.0f);
    CHECK(gpu.lights[1].params.y == -1.0f);  // the second sun has no shadow
    CHECK(shadows.pointCount == 2 && shadows.point[0] == 2 && shadows.point[1] == 4);
    CHECK(gpu.lights[2].params.y == 0.0f && gpu.lights[3].params.y == -1.0f && gpu.lights[4].params.y == 1.0f);

    // Not asked for: none.
    GpuLighting plain = buildLighting(lights, env, true);
    for (int i = 0; i < 5; i++) CHECK(plain.lights[i].params.y == -1.0f);

    // Ray-traced shadows: every light that casts one is marked, with no limit and no maps (nothing is assigned).
    {
        std::vector<SceneLight> crowd = {sunLight(glm::vec3(0, 0, -1)), sunLight(glm::vec3(1, 0, -1)), bulbLight(glm::vec3(5, 0, 3), false)};
        for (int i = 0; i < 7; i++) crowd.push_back(bulbLight(glm::vec3(static_cast<float>(i), 0, 2)));
        ShadowAssignment none;
        GpuLighting traced = buildLighting(crowd, env, true, nullptr, &none, true);
        CHECK(none.directional == -1 && none.pointCount == 0);
        CHECK(traced.lights[0].params.y >= 0.0f && traced.lights[1].params.y >= 0.0f);  // both suns, not just the first
        CHECK(traced.lights[2].params.y == -1.0f);                                     // this one doesn't cast
        for (int i = 3; i < 10; i++) CHECK(traced.lights[i].params.y >= 0.0f);          // all seven bulbs, past the four maps
        GpuLighting off = buildLighting(crowd, env, true, nullptr, nullptr, false);
        CHECK(off.lights[0].params.y == -1.0f);                                         // not asked for: none
    }

    // Only four bulbs, however many there are.
    std::vector<SceneLight> many;
    for (int i = 0; i < 7; i++) many.push_back(bulbLight(glm::vec3(static_cast<float>(i), 0, 2)));
    ShadowAssignment manyShadows;
    GpuLighting manyGpu = buildLighting(many, env, true, nullptr, &manyShadows);
    CHECK(manyShadows.pointCount == Shadows::kMaxPointShadows);
    int slots = 0;
    for (int i = 0; i < 7; i++) slots += manyGpu.lights[i].params.y >= 0.0f ? 1 : 0;
    CHECK(slots == Shadows::kMaxPointShadows);

    // Lights that are skipped (turned off, or at no intensity) don't use up slots, and the indices follow the packed order.
    SceneLight off = bulbLight(glm::vec3(0, 0, 9));
    off.settings.enabled = false;
    SceneLight dark = bulbLight(glm::vec3(0, 0, 8));
    dark.settings.intensity = 0.0f;
    ShadowAssignment skipShadows;
    GpuLighting skipGpu = buildLighting({off, dark, bulbLight(glm::vec3(1, 1, 1))}, env, true, nullptr, &skipShadows);
    CHECK(skipGpu.envParams.w == 1.0f && skipShadows.pointCount == 1 && skipShadows.point[0] == 0);
    CHECK(nearVec(skipShadows.lights[0].position, glm::vec3(1, 1, 1)));

    // The default rig's key light casts a shadow and its fill doesn't.
    ShadowAssignment rig;
    GpuLighting rigGpu = buildLighting({}, env, true, nullptr, &rig);
    CHECK(rigGpu.envParams.w == 2.0f && rig.directional == 0 && rigGpu.lights[0].params.y == 0.0f && rigGpu.lights[1].params.y == -1.0f);
}

static void testLightSizesForSoftShadows() {
    SceneLight sun = sunLight(glm::vec3(0, 0, -1));
    sun.settings.angularDiameterDeg = 0.53f;
    CHECK(near(packLight(sun).params.z, 2.0f * std::tan(glm::radians(0.53f) * 0.5f), 1e-6f));
    sun.settings.angularDiameterDeg = 0.0f;
    CHECK(packLight(sun).params.z == 0.0f);  // a point-like sun casts hard shadows
    SceneLight bulb = bulbLight(glm::vec3(0));
    bulb.settings.radius = 0.25f;
    bulb.settings.range = 18.0f;
    const GpuLight packed = packLight(bulb);
    CHECK(packed.params.z == 0.5f && packed.params.w == 18.0f);  // diameter, and how far it reaches
}

static void testApplyShadowFrame() {
    EnvironmentLighting env;
    std::vector<SceneLight> lights = {sunLight(glm::normalize(glm::vec3(-0.4f, 0.5f, -1.0f))), bulbLight(glm::vec3(2, 3, 4))};
    lights[1].settings.range = 12.0f;
    ShadowAssignment shadows;
    GpuLighting gpu = buildLighting(lights, env, true, nullptr, &shadows);

    const glm::mat4 view = viewFrom(glm::vec3(6, 6, 4), glm::vec3(0, 0, 0));
    const glm::mat4 projection = engineProjection(60.0f, 1.6f);
    Aabb casters;
    casters.expand(glm::vec3(-5, -5, 0));
    casters.expand(glm::vec3(5, 5, 3));
    const ShadowFrame frame = applyShadowFrame(gpu, shadows, view, projection, 0.1f, 60.0f, casters);

    CHECK(frame.directional.valid && frame.directional.count == kCascadeCount);
    CHECK(gpu.dirShadowGlobal.x == static_cast<float>(kCascadeCount) && gpu.dirShadowGlobal.y == 1.0f);
    CHECK(gpu.dirShadowGlobal.z == kCascadeSplitBlend && gpu.dirShadowGlobal.w == 60.0f);
    for (int c = 0; c < kCascadeCount; c++) {
        CHECK(gpu.dirShadowParams[c].y == frame.directional.maps[c].depthRange && gpu.dirShadowParams[c].z == frame.directional.maps[c].orthoWidth);
        CHECK(near(gpu.dirShadowParams[c].x, 1.0f / static_cast<float>(kCascadeMapSize), 1e-9f));
        CHECK(gpu.dirShadowMatrix[c] == frame.directional.maps[c].viewProjection);
        CHECK(gpu.dirShadowSplits[c] == frame.directional.splitDepth[c]);
    }

    CHECK(frame.pointCount == 1 && frame.pointPosition[0] == glm::vec3(2, 3, 4) && frame.pointReach[0] == 12.0f);
    for (int f = 0; f < 6; f++) CHECK(gpu.pointShadow[f] == frame.pointFaces[0][f]);
    CHECK(gpu.shadowGlobal.x == static_cast<float>(kPointMapSize) && gpu.shadowGlobal.y == kPointNearPlane);

    // A sun that can't have a map (no direction) loses its slot, and the shader is told there is no sun shadow.
    lights[0].direction = glm::vec3(0.0f);
    ShadowAssignment bad;
    GpuLighting badGpu = buildLighting(lights, env, true, nullptr, &bad);
    CHECK(badGpu.lights[0].params.y == 0.0f);
    const ShadowFrame badFrame = applyShadowFrame(badGpu, bad, view, projection, 0.1f, 60.0f, casters);
    CHECK(!badFrame.directional.valid && badGpu.lights[0].params.y == -1.0f && badGpu.dirShadowGlobal.y == 0.0f);

    // A shadow distance that isn't past the camera's near plane can't have maps either.
    ShadowAssignment ok;
    lights[0].direction = glm::vec3(0, 0, -1);
    GpuLighting okGpu = buildLighting(lights, env, true, nullptr, &ok);
    const ShadowFrame empty = applyShadowFrame(okGpu, ok, view, projection, 0.1f, 0.05f, casters);
    CHECK(!empty.directional.valid && okGpu.lights[0].params.y == -1.0f);
}

int main() {
    testFrustumSliceCorners();
    testSunMapCoversTheView();
    testSunMapReachesCastersTowardTheSun();
    testSunMapHoldsStill();
    testSunMapStraightDownAndBadInput();
    testCascadeSplits();
    testCascadesCoverTheView();
    testNearCascadesAreSharper();
    testCascadesHoldStill();
    testSphereOverlapsMap();
    testAabb();
    testPointFaceLooksAlongItsAxis();
    testEveryDirectionIsInItsFace();
    testShadowSlots();
    testLightSizesForSoftShadows();
    testApplyShadowFrame();

    if (failures == 0) std::printf("ShadowTest: all passed\n");
    else std::printf("ShadowTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
