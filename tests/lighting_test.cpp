// Tests for lights and environments (include/rendering/EnvironmentMap.h, LightingData.h, core/LightSettings.h):
// where a direction lands in an HDRI, the diffuse lighting a sky gives, the reflection chain, half floats, and the
// block of lights the shader reads. No window or GPU.
//
//   cmake --build . --target LightingTest --config Debug
//   build/tests/LightingTest.exe

#include "core/EnvironmentSettings.h"
#include "core/LightSettings.h"
#include "rendering/EnvironmentMap.h"
#include "rendering/LightingData.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace Propulsion;
using namespace Propulsion::Environment;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
static bool nearVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

static Image constantImage(int w, int h, const glm::vec3& c) {
    Image img(w, h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) img.set(x, y, c);
    return img;
}

// A dark sky with one bright spot in the direction `d`.
static Image spotImage(int w, int h, const glm::vec3& d, float brightness) {
    Image img = constantImage(w, h, glm::vec3(0.0f));
    const glm::vec2 uv = directionToUv(d);
    const int x = (std::min)(w - 1, static_cast<int>(uv.x * static_cast<float>(w)));
    const int y = (std::min)(h - 1, static_cast<int>(uv.y * static_cast<float>(h)));
    img.set(x, y, glm::vec3(brightness));
    return img;
}

static float luminance(const glm::vec3& c) { return (c.x + c.y + c.z) / 3.0f; }

// ---- Mapping ----------------------------------------------------------------------------------------------------

static void testMappingLandmarks() {
    // The middle of the panorama is forward (+Y); the right of the middle is +X; the top row is up (+Z).
    glm::vec2 forward = directionToUv(glm::vec3(0, 1, 0));
    CHECK(near(forward.x, 0.5f) && near(forward.y, 0.5f));
    glm::vec2 right = directionToUv(glm::vec3(1, 0, 0));
    CHECK(near(right.x, 0.75f) && near(right.y, 0.5f));
    glm::vec2 left = directionToUv(glm::vec3(-1, 0, 0));
    CHECK(near(left.x, 0.25f) && near(left.y, 0.5f));
    glm::vec2 up = directionToUv(glm::vec3(0, 0, 1));
    CHECK(near(up.y, 0.0f));
    glm::vec2 down = directionToUv(glm::vec3(0, 0, -1));
    CHECK(near(down.y, 1.0f));
    // Behind is the seam: the edge of the picture.
    glm::vec2 back = directionToUv(glm::vec3(0, -1, 0));
    CHECK(near(back.x, 0.0f, 1e-3f) || near(back.x, 1.0f, 1e-3f));
}

static void testMappingRoundTrip() {
    for (float u = 0.03f; u < 1.0f; u += 0.11f) {
        for (float v = 0.05f; v < 1.0f; v += 0.13f) {
            const glm::vec3 d = uvToDirection(glm::vec2(u, v));
            CHECK(near(glm::length(d), 1.0f));
            const glm::vec2 back = directionToUv(d);
            CHECK(near(back.x, u, 2e-3f) && near(back.y, v, 2e-3f));
        }
    }
}

static void testSampleConstantAndWrap() {
    const Image img = constantImage(32, 16, glm::vec3(0.25f, 0.5f, 1.0f));
    CHECK(nearVec(sample(img, glm::vec3(0.3f, -0.8f, 0.2f)), glm::vec3(0.25f, 0.5f, 1.0f)));
    CHECK(nearVec(sample(img, glm::vec3(0, 0, 1)), glm::vec3(0.25f, 0.5f, 1.0f)));   // the pole
    CHECK(nearVec(sample(img, glm::vec3(0, -1, 0)), glm::vec3(0.25f, 0.5f, 1.0f)));  // the seam
    // An empty image is black, not a crash.
    CHECK(nearVec(sample(Image(), glm::vec3(0, 1, 0)), glm::vec3(0.0f)));
}

static void testSampleYaw() {
    // A bright pixel straight ahead (+Y). Turning the sky a quarter turn counter-clockwise (seen from above) moves
    // ahead to -X.
    const Image img = spotImage(256, 128, glm::vec3(0, 1, 0), 10.0f);
    const float quarter = kPi * 0.5f;
    CHECK(luminance(sample(img, glm::vec3(0, 1, 0), 0.0f)) > 1.0f);
    CHECK(luminance(sample(img, glm::vec3(-1, 0, 0), quarter)) > 1.0f);
    CHECK(luminance(sample(img, glm::vec3(0, 1, 0), quarter)) < 0.5f);
    CHECK(luminance(sample(img, glm::vec3(1, 0, 0), quarter)) < 0.5f);
}

// ---- Diffuse lighting from the sky -----------------------------------------------------------------------------

static void testShConstantSky() {
    // A sky of radiance 1 lights a white surface with 1 (irradiance pi, divided by pi), whichever way it faces.
    const Image img = constantImage(64, 32, glm::vec3(1.0f));
    const Sh9 sh = projectDiffuseSh(img);
    const glm::vec3 normals[] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {0, 1, 0}, {-0.5f, 0.3f, 0.8f}};
    for (const glm::vec3& n : normals) CHECK(nearVec(evalDiffuseSh(sh, n), glm::vec3(1.0f), 0.01f));

    const Image tinted = constantImage(64, 32, glm::vec3(0.2f, 0.4f, 0.8f));
    CHECK(nearVec(evalDiffuseSh(projectDiffuseSh(tinted), glm::vec3(0, 1, 0)), glm::vec3(0.2f, 0.4f, 0.8f), 0.01f));
}

static void testShHemisphereSky() {
    // Bright above the horizon, dark below: faces up are lit, faces down are not, sideways is about half.
    Image img(64, 32);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) img.set(x, y, glm::vec3(y < 16 ? 1.0f : 0.0f));
    const Sh9 sh = projectDiffuseSh(img);
    CHECK(luminance(evalDiffuseSh(sh, glm::vec3(0, 0, 1))) > 0.9f);
    CHECK(luminance(evalDiffuseSh(sh, glm::vec3(0, 0, -1))) < 0.1f);
    CHECK(near(luminance(evalDiffuseSh(sh, glm::vec3(1, 0, 0))), 0.5f, 0.1f));
}

// The light a white surface facing `n` receives from the picture, divided by pi, summed pixel by pixel.
static glm::vec3 bruteForceDiffuse(const Image& img, const glm::vec3& n, float yawRadians = 0.0f) {
    glm::dvec3 sum(0.0);
    for (int y = 0; y < img.height; y++) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(img.height);
        const double solidAngle = (2.0 * kPi / img.width) * (kPi / img.height) * std::sin(v * kPi);
        for (int x = 0; x < img.width; x++) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(img.width);
            const glm::vec3 d = rotateZ(uvToDirection(glm::vec2(u, v)), yawRadians);
            const double cosine = (std::max)(0.0f, glm::dot(glm::normalize(n), d));
            sum += glm::dvec3(img.get(x, y)) * (cosine * solidAngle);
        }
    }
    return glm::vec3(sum / static_cast<double>(kPi));
}

static void testShMatchesBruteForce() {
    // For a smooth sky, nine coefficients give the same answer as adding up every pixel (to a few percent). This is
    // what catches a wrong weight on any band.
    const Image sky = makeGradientSky(64);
    const Sh9 sh = projectDiffuseSh(sky);
    const glm::vec3 normals[] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0},
                                 {0.6f, 0.5f, 0.6f}, {-0.3f, 0.8f, -0.5f}};
    for (const glm::vec3& n : normals) {
        const glm::vec3 exact = bruteForceDiffuse(sky, n);
        const glm::vec3 approx = evalDiffuseSh(sh, n);
        CHECK(nearVec(approx, exact, 0.03f));
    }

    // And with the sky turned: the same sum, with every direction turned.
    const float yaw = 0.9f;
    const Sh9 turned = projectDiffuseSh(sky, yaw);
    for (const glm::vec3& n : normals) CHECK(nearVec(evalDiffuseSh(turned, n), bruteForceDiffuse(sky, n, yaw), 0.03f));

    // A sky with one lobe of light: sideways structure, so the l=1 and l=2 bands matter.
    Image lobe = constantImage(64, 32, glm::vec3(0.05f));
    for (int y = 8; y < 20; y++) for (int x = 12; x < 28; x++) lobe.set(x, y, glm::vec3(2.0f, 1.5f, 1.0f));
    const Sh9 lobeSh = projectDiffuseSh(lobe);
    for (const glm::vec3& n : normals) CHECK(nearVec(evalDiffuseSh(lobeSh, n), bruteForceDiffuse(lobe, n), 0.25f));
}

static void testShSunDirectionAndYaw() {
    // A sun low on the horizon ahead (+Y): the face toward it is brighter than the face away from it.
    Image img = constantImage(64, 32, glm::vec3(0.0f));
    const glm::vec2 uv = directionToUv(glm::vec3(0, 1, 0));
    for (int y = 14; y < 18; y++) for (int x = 30; x < 34; x++) img.set(x, y, glm::vec3(50.0f));
    (void)uv;
    const Sh9 sh = projectDiffuseSh(img, 0.0f);
    CHECK(luminance(evalDiffuseSh(sh, glm::vec3(0, 1, 0))) > luminance(evalDiffuseSh(sh, glm::vec3(0, -1, 0))) * 3.0f);

    // Turned a quarter turn counter-clockwise, the sun is at -X.
    const Sh9 turned = projectDiffuseSh(img, kPi * 0.5f);
    CHECK(luminance(evalDiffuseSh(turned, glm::vec3(-1, 0, 0))) > luminance(evalDiffuseSh(turned, glm::vec3(1, 0, 0))) * 3.0f);
    CHECK(luminance(evalDiffuseSh(turned, glm::vec3(-1, 0, 0))) > luminance(evalDiffuseSh(turned, glm::vec3(0, 1, 0))));
}

// ---- Reflection chain -----------------------------------------------------------------------------------------

static void testChainShape() {
    const Image sky = constantImage(256, 128, glm::vec3(1.0f));
    const Prepared p = prepare(sky);
    // 256, 128, 64, 32, 16, 8 wide.
    CHECK(p.chain.levelCount() == 6);
    int expected = 256;
    for (const Image& level : p.chain.levels) {
        CHECK(level.width == expected);
        CHECK(level.height == expected / 2);
        expected /= 2;
    }
    CHECK(p.diffuseSource.width <= 64 && p.diffuseSource.width >= 8);
}

static void testChainOtherSizes() {
    // Not a power of two, and not 2:1: the chain is still a power of two, 2:1.
    const Image odd = constantImage(300, 100, glm::vec3(2.0f));
    const Prepared p = prepare(odd);
    CHECK(p.chain.levels[0].width == 256 && p.chain.levels[0].height == 128);
    CHECK(nearVec(p.chain.levels[0].get(10, 10), glm::vec3(2.0f), 0.01f));

    // Capped: a huge picture comes out no wider than the cap.
    const Image big = constantImage(512, 256, glm::vec3(1.0f));
    const Prepared capped = prepare(big, 64);
    CHECK(capped.chain.levels[0].width == 64);

    // Tiny pictures are made big enough to filter.
    const Prepared tiny = prepare(constantImage(4, 2, glm::vec3(1.0f)));
    CHECK(tiny.chain.levels[0].width == kMinBaseWidth);
    CHECK(tiny.chain.levelCount() >= 2);
}

static void testChainKeepsConstantSky() {
    const glm::vec3 c(0.3f, 0.6f, 0.9f);
    const Prepared p = prepare(constantImage(256, 128, c));
    for (const Image& level : p.chain.levels) {
        for (int y = 0; y < level.height; y++) {
            for (int x = 0; x < level.width; x++) {
                if (!nearVec(level.get(x, y), c, 0.01f)) {
                    CHECK(false);
                    return;
                }
            }
        }
    }
}

static void testChainBlursASpot() {
    // A bright spot spreads out as roughness grows: its peak falls with each level, and the light it carries stays
    // about the same.
    const Image sky = spotImage(256, 128, glm::vec3(0, 1, 0), 1000.0f);
    const Prepared p = prepare(sky);

    float previousPeak = std::numeric_limits<float>::max();
    for (size_t i = 0; i < p.chain.levels.size(); i++) {
        float peak = 0.0f;
        const Image& level = p.chain.levels[i];
        for (size_t k = 0; k < level.rgb.size(); k++) peak = (std::max)(peak, level.rgb[k]);
        CHECK(peak <= previousPeak * 1.001f);
        previousPeak = peak;
    }

    // The last level is smooth: no pixel is more than a few times the average.
    const Image& last = p.chain.levels.back();
    float sum = 0.0f, peak = 0.0f;
    for (float v : last.rgb) { sum += v; peak = (std::max)(peak, v); }
    const float mean = sum / static_cast<float>(last.rgb.size());
    CHECK(mean > 0.0f);
    CHECK(peak < mean * 12.0f);

    // Reflections of the sun line up with where the sun is: at a mid level the brightest pixel is still near ahead.
    const Image& mid = p.chain.levels[2];
    int bx = 0, by = 0;
    float best = -1.0f;
    for (int y = 0; y < mid.height; y++) for (int x = 0; x < mid.width; x++) {
        if (mid.get(x, y).x > best) { best = mid.get(x, y).x; bx = x; by = y; }
    }
    const glm::vec3 d = uvToDirection(glm::vec2((static_cast<float>(bx) + 0.5f) / static_cast<float>(mid.width),
                                                (static_cast<float>(by) + 0.5f) / static_cast<float>(mid.height)));
    CHECK(glm::dot(d, glm::vec3(0, 1, 0)) > 0.95f);
}

// ---- Half floats ----------------------------------------------------------------------------------------------

static void testHalfFloats() {
    CHECK(floatToHalf(1.0f) == 0x3C00);
    CHECK(floatToHalf(0.0f) == 0x0000);
    CHECK(floatToHalf(-2.0f) == 0xC000);
    CHECK(floatToHalf(65504.0f) == 0x7BFF);
    CHECK(floatToHalf(1.0e6f) == 0x7C00);  // too big: infinity
    CHECK(halfToFloat(0x3C00) == 1.0f);
    CHECK(halfToFloat(0x3800) == 0.5f);
    CHECK(halfToFloat(0x7BFF) == 65504.0f);

    const float samples[] = {0.0f, 1.0f, 0.5f, 3.14159f, 100.0f, 1234.5f, 0.001f, 6.1e-5f, 1e-6f};
    for (float v : samples) {
        const float back = halfToFloat(floatToHalf(v));
        // Half floats keep about three digits; the smallest values lose a little more.
        CHECK(std::fabs(back - v) <= (std::max)(std::fabs(v) * 1e-3f, 1e-7f));
    }
}

static void testToRgba16f() {
    Image img(2, 1);
    img.set(0, 0, glm::vec3(1.0f, 0.5f, 0.25f));
    img.set(1, 0, glm::vec3(1.0e9f, -3.0f, std::numeric_limits<float>::quiet_NaN()));
    const std::vector<uint16_t> out = toRgba16f(img);
    CHECK(out.size() == 8);
    CHECK(halfToFloat(out[0]) == 1.0f && halfToFloat(out[1]) == 0.5f && halfToFloat(out[2]) == 0.25f && halfToFloat(out[3]) == 1.0f);
    CHECK(halfToFloat(out[4]) > 60000.0f && halfToFloat(out[4]) <= 65504.0f);  // a too-bright pixel is clamped, not infinite
    CHECK(halfToFloat(out[5]) == 0.0f);                                          // negative: black
    CHECK(halfToFloat(out[6]) == 0.0f);                                          // NaN: black
}

// ---- Gradient sky ---------------------------------------------------------------------------------------------

static void testGradientSky() {
    const SkyGradient g;
    const Image sky = makeGradientSky(64, g);
    CHECK(sky.width == 64 && sky.height == 32);
    // Blue overhead, the ground color below, something in between at the horizon.
    const glm::vec3 overhead = sample(sky, glm::vec3(0, 0, 1));
    const glm::vec3 below = sample(sky, glm::vec3(0, 0, -1));
    const glm::vec3 horizon = sample(sky, glm::vec3(0, 1, 0));
    CHECK(overhead.z > overhead.x);  // bluish
    CHECK(luminance(below) < luminance(horizon));
    CHECK(luminance(overhead) < luminance(horizon) + 0.2f);
    // A fully sky-lit up-facing surface gets a sensible amount of light (neither dark nor blown out).
    const float lit = luminance(evalDiffuseSh(projectDiffuseSh(sky), glm::vec3(0, 0, 1)));
    CHECK(lit > 0.2f && lit < 1.0f);
}

// ---- Lights ----------------------------------------------------------------------------------------------------

static void testLightSettings() {
    LightSettings l = LightSettings::point();
    CHECK(l.enabled && l.type == LightSettings::Type::Point);
    LightSettings sun = LightSettings::directional();
    CHECK(sun.enabled && sun.type == LightSettings::Type::Directional && sun.intensity == 3.0f);

    LightSettings bad;
    bad.intensity = -4.0f;
    bad.range = std::numeric_limits<float>::quiet_NaN();
    bad.radius = -1.0f;
    bad.color = glm::vec3(-1.0f, 5000.0f, std::numeric_limits<float>::quiet_NaN());
    bad.angularDiameterDeg = 500.0f;
    bad.sanitize();
    CHECK(bad.intensity == 0.0f);
    CHECK(bad.range >= LightSettings::kMinRange);
    CHECK(bad.radius == 0.0f);
    CHECK(bad.color.x == 0.0f && bad.color.y == 1000.0f && bad.color.z == 0.0f);
    CHECK(bad.angularDiameterDeg == LightSettings::kMaxAngularDiameter);

    // An Object with no rotation shines forward (+Y); turned a quarter about Z it shines along -X.
    CHECK(nearVec(lightTravelDirection(glm::quat(1, 0, 0, 0)), glm::vec3(0, 1, 0)));
    const glm::quat quarter = glm::angleAxis(kPi * 0.5f, glm::vec3(0, 0, 1));
    CHECK(nearVec(lightTravelDirection(quarter), glm::vec3(-1, 0, 0)));
    // Tipped down 90 degrees about X (pitch), the sun shines straight down, which is how it lights a floor.
    const glm::quat pitchDown = glm::angleAxis(-kPi * 0.5f, glm::vec3(1, 0, 0));
    CHECK(nearVec(lightTravelDirection(pitchDown), glm::vec3(0, 0, -1)));

    // Aiming: the rotation that points forward along any direction makes a light shine that way.
    const glm::vec3 aims[] = {{0, 0, -1}, {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-0.4f, 0.5f, -1.0f}, {0, 0, 1}, {3, -2, 0.5f}};
    for (const glm::vec3& aim : aims) {
        const glm::vec3 expected = glm::normalize(aim);
        CHECK(nearVec(lightTravelDirection(rotationFromForwardTo(aim)), expected, 1e-3f));
    }

    LightSettings a = sun, b = sun;
    CHECK(a == b);
    b.intensity = 4.0f;
    CHECK(a != b);
}

static void testEnvironmentSettings() {
    EnvironmentSettings e;
    CHECK(e.hdriPath.empty() && e.intensity == 1.0f && e.rotationDegrees == 0.0f && e.showBackground);
    e.intensity = -1.0f;
    e.rotationDegrees = std::numeric_limits<float>::infinity();
    e.sanitize();
    CHECK(e.intensity == 0.0f && e.rotationDegrees == 0.0f);
    e.intensity = 1.0e9f;
    e.sanitize();
    CHECK(e.intensity == EnvironmentSettings::kMaxIntensity);
}

static void testPackLights() {
    SceneLight sun;
    sun.settings = LightSettings::directional();
    sun.settings.color = glm::vec3(1.0f, 0.5f, 0.25f);
    sun.settings.intensity = 4.0f;
    sun.direction = glm::vec3(0, 0, -1);  // shining straight down
    const GpuLight packedSun = packLight(sun);
    CHECK(packedSun.positionType == glm::vec4(0, 0, 1, 0));        // the direction TO the light: up
    CHECK(packedSun.colorRange == glm::vec4(4.0f, 2.0f, 1.0f, 0.0f));
    CHECK(packedSun.params.x == 1.0f);

    SceneLight bulb;
    bulb.settings = LightSettings::point();
    bulb.settings.intensity = 10.0f;
    bulb.settings.range = 7.0f;
    bulb.position = glm::vec3(1, 2, 3);
    const GpuLight packedBulb = packLight(bulb);
    CHECK(packedBulb.positionType == glm::vec4(1, 2, 3, 1));
    CHECK(packedBulb.colorRange == glm::vec4(10.0f, 10.0f, 10.0f, 7.0f));

    // Not normalized on the way in: the packed direction still is.
    sun.direction = glm::vec3(0, 0, -5);
    CHECK(near(glm::length(glm::vec3(packLight(sun).positionType)), 1.0f));
}

static void testBuildLighting() {
    EnvironmentLighting env;
    env.intensity = 2.0f;
    env.yawRadians = 0.5f;
    env.maxLod = 5.0f;
    env.sh.c[0] = glm::vec3(1, 2, 3);
    env.sh.c[8] = glm::vec3(4, 5, 6);

    // No lights: the default rig (a key and a fill), which is how scenes looked before lights were objects.
    GpuLighting rig = buildLighting({}, env, true);
    CHECK(rig.envParams == glm::vec4(2.0f, 0.5f, 5.0f, 2.0f));
    CHECK(rig.sh[0] == glm::vec4(1, 2, 3, 0) && rig.sh[8] == glm::vec4(4, 5, 6, 0));
    CHECK(rig.lights[0].positionType.w == 0.0f && rig.lights[1].positionType.w == 0.0f);
    CHECK(rig.lights[0].params.x == 1.0f && rig.lights[1].params.x == 0.0f);  // the fill has no specular
    CHECK(rig.lights[0].positionType.z > 0.0f);                                // light comes from above
    // ... unless the caller doesn't want it.
    CHECK(buildLighting({}, env, false).envParams.w == 0.0f);

    // A scene with its own light: just that one.
    SceneLight bulb;
    bulb.settings = LightSettings::point();
    GpuLighting one = buildLighting({bulb}, env, true);
    CHECK(one.envParams.w == 1.0f && one.lights[0].positionType.w == 1.0f);

    // A light turned down to nothing is still the scene's light, so the rig does not come back; and a light that is
    // off (no component) is not a light at all.
    SceneLight dark = bulb;
    dark.settings.intensity = 0.0f;
    CHECK(buildLighting({dark}, env, true).envParams.w == 0.0f);
    SceneLight off = bulb;
    off.settings.enabled = false;
    CHECK(buildLighting({off}, env, true).envParams.w == 2.0f);

    // Too many lights: the first kMaxLights are kept and the rest are counted.
    std::vector<SceneLight> many(kMaxLights + 5, bulb);
    int dropped = 0;
    GpuLighting full = buildLighting(many, env, true, &dropped);
    CHECK(full.envParams.w == static_cast<float>(kMaxLights));
    CHECK(dropped == 5);
}

int main() {
    testMappingLandmarks();
    testMappingRoundTrip();
    testSampleConstantAndWrap();
    testSampleYaw();
    testShConstantSky();
    testShHemisphereSky();
    testShMatchesBruteForce();
    testShSunDirectionAndYaw();
    testChainShape();
    testChainOtherSizes();
    testChainKeepsConstantSky();
    testChainBlursASpot();
    testHalfFloats();
    testToRgba16f();
    testGradientSky();
    testLightSettings();
    testEnvironmentSettings();
    testPackLights();
    testBuildLighting();

    if (failures == 0) std::printf("All lighting tests passed.\n");
    else std::printf("%d lighting check(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
