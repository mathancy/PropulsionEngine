// Tests for the ray tracing foundation's pure parts (include/rendering/RayTracingSupport.h): what a GPU needs to count as
// ray-tracing capable, which GPU is chosen, the transform layout the acceleration structure builder wants, and that the
// instance table's layout is the one the shaders read (shaders/raytracing.glsl). No window or GPU.
//
//   cmake --build . --target RayTracingTest --config Debug
//   build/tests/RayTracingTest.exe

#include "core/RenderMode.h"
#include "rendering/RayTracing.h"
#include "rendering/RayTracingSupport.h"
#include "rendering/TextureSlots.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
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

static bool close(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

static RayTracingCaps fullCaps() {
    RayTracingCaps caps;
    caps.accelerationStructure = caps.rayQuery = caps.rayTracingPipeline = caps.deferredHostOperations = true;
    caps.bufferDeviceAddress = caps.descriptorIndexing = caps.scalarBlockLayout = caps.shaderInt64 = true;
    return caps;
}

static void testCapabilities() {
    CHECK(!RayTracingCaps{}.canQuery());
    CHECK(!RayTracingCaps{}.canTracePipeline());
    CHECK(fullCaps().canQuery());
    CHECK(fullCaps().canTracePipeline());

    // Ray queries need every one of their parts: take each away in turn.
    bool RayTracingCaps::*parts[] = {&RayTracingCaps::accelerationStructure, &RayTracingCaps::rayQuery,
                                     &RayTracingCaps::bufferDeviceAddress, &RayTracingCaps::descriptorIndexing,
                                     &RayTracingCaps::scalarBlockLayout, &RayTracingCaps::shaderInt64};
    for (auto part : parts) {
        RayTracingCaps caps = fullCaps();
        caps.*part = false;
        CHECK(!caps.canQuery());
        CHECK(!caps.canTracePipeline());
    }

    // The pipeline is on top of queries, and needs its deferred-operations partner.
    RayTracingCaps noPipeline = fullCaps();
    noPipeline.rayTracingPipeline = false;
    CHECK(noPipeline.canQuery() && !noPipeline.canTracePipeline());
    RayTracingCaps noDeferred = fullCaps();
    noDeferred.deferredHostOperations = false;
    CHECK(noDeferred.canQuery() && !noDeferred.canTracePipeline());
}

static void testGpuChoice() {
    const GpuCandidate integrated{true, false, false, 2ull << 30};
    const GpuCandidate discreteOld{true, true, false, 8ull << 30};
    const GpuCandidate discreteRt{true, true, true, 12ull << 30};
    const GpuCandidate discreteRtBig{true, true, true, 24ull << 30};
    const GpuCandidate integratedRt{true, false, true, 2ull << 30};
    const GpuCandidate unusable{false, true, true, 48ull << 30};

    CHECK(gpuIsBetter(discreteOld, integrated));
    CHECK(!gpuIsBetter(integrated, discreteOld));
    CHECK(gpuIsBetter(discreteRt, discreteOld));              // ray queries beat a bigger plain card
    CHECK(gpuIsBetter(integratedRt, discreteOld));            // ... and beat being discrete
    CHECK(gpuIsBetter(discreteRtBig, discreteRt));            // then more memory wins
    CHECK(!gpuIsBetter(discreteRt, discreteRtBig));
    CHECK(!gpuIsBetter(discreteRt, discreteRt));              // a tie keeps the earlier one
    CHECK(gpuIsBetter(integrated, unusable));                 // an unusable GPU never wins, whatever it has
    CHECK(!gpuIsBetter(unusable, integrated));
}

static void testTransformLayout() {
    // The builder wants three rows of four floats; glm stores four columns of three (plus the w row).
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3)) *
                            glm::rotate(glm::mat4(1.0f), glm::radians(40.0f), glm::vec3(0, 0, 1)) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(2, 3, 4));
    const std::array<float, 12> rows = rowMajor3x4(model);
    for (int i = 0; i < 6; i++) {
        const glm::vec3 p(static_cast<float>(i) - 2.0f, 0.5f * static_cast<float>(i), 1.0f - static_cast<float>(i));
        const glm::vec3 expected = glm::vec3(model * glm::vec4(p, 1.0f));
        for (int row = 0; row < 3; row++) {
            const float got = rows[row * 4 + 0] * p.x + rows[row * 4 + 1] * p.y + rows[row * 4 + 2] * p.z + rows[row * 4 + 3];
            CHECK(close(got, expected[row], 1e-4f));
        }
    }
    // The translation is the last number of each row.
    CHECK(close(rows[3], 1.0f) && close(rows[7], 2.0f) && close(rows[11], 3.0f));
    const std::array<float, 12> identity = rowMajor3x4(glm::mat4(1.0f));
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) CHECK(identity[row * 4 + column] == (row == column ? 1.0f : 0.0f));
    }
}

static void testAlignUp() {
    CHECK(alignUp(0, 256) == 0);
    CHECK(alignUp(1, 256) == 256);
    CHECK(alignUp(256, 256) == 256);
    CHECK(alignUp(257, 256) == 512);
    CHECK(alignUp(1000, 0) == 1000);
    CHECK(alignUp(0x7fffffff01ull, 128) == 0x7fffffff80ull);
}

static void testInstanceTableLayout() {
    // shaders/raytracing.glsl reads this struct with scalar block layout: two addresses, a 3x3 matrix, an index.
    CHECK(sizeof(GpuRtInstance) == 56);
    CHECK(offsetof(GpuRtInstance, vertexAddress) == 0);
    CHECK(offsetof(GpuRtInstance, indexAddress) == 8);
    CHECK(offsetof(GpuRtInstance, normalMatrix) == 16);
    CHECK(offsetof(GpuRtInstance, materialIndex) == 52);
    CHECK(sizeof(glm::mat3) == 36);  // nine floats, no padding between columns
    // And the vertex layout that its shader struct reads, and the acceleration structure builds from.
    CHECK(sizeof(Vertex) == 32);
    CHECK(offsetof(Vertex, position) == 0);
    CHECK(offsetof(Vertex, normal) == 12);
    CHECK(offsetof(Vertex, texCoords) == 24);
}

static void testRenderModes() {
    CHECK(renderModeFromInt(0) == RenderMode::Raster && renderModeFromInt(1) == RenderMode::HybridRT && renderModeFromInt(2) == RenderMode::PathTracer);
    CHECK(renderModeFromInt(-1) == RenderMode::Raster && renderModeFromInt(3) == RenderMode::Raster && renderModeFromInt(1000) == RenderMode::Raster);
    // Raster always works; the ray-traced modes need ray queries, and otherwise the viewport falls back to Raster.
    for (int i = 0; i < kRenderModeCount; i++) {
        const RenderMode mode = renderModeFromInt(i);
        CHECK(renderModeAvailable(mode, true));
        CHECK(effectiveRenderMode(mode, true) == mode);
        CHECK(effectiveRenderMode(mode, false) == RenderMode::Raster);
    }
    CHECK(renderModeAvailable(RenderMode::Raster, false) && !renderModeAvailable(RenderMode::HybridRT, false) && !renderModeAvailable(RenderMode::PathTracer, false));
}

static void testTextureSlots() {
    TextureSlots slots(4);
    CHECK(slots.used() == 1 && slots.capacity() == 4);  // slot 0 is the fallback
    CHECK(slots.slotFor(100, 7) == 1);
    CHECK(slots.slotFor(200, 7) == 2);                   // another picture, same sampler
    CHECK(slots.slotFor(100, 8) == 3);                   // the same picture with another sampler is another texture
    CHECK(slots.slotFor(100, 7) == 1 && slots.slotFor(200, 7) == 2);  // asking again gives the same slot, never a new one
    CHECK(slots.used() == 4);
    CHECK(slots.slotFor(300, 7) == TextureSlots::kNone);  // full
    CHECK(slots.slotFor(100, 7) == 1);                    // ... and what is in it still is
    CHECK(slots.has(200, 7) && !slots.has(300, 7));

    // A table with room for nothing but the fallback never hands out a slot
    TextureSlots tiny(1);
    CHECK(tiny.slotFor(1, 1) == TextureSlots::kNone && tiny.used() == 1);
    TextureSlots none(0);
    CHECK(none.slotFor(1, 1) == TextureSlots::kNone && none.used() == 0);

    // Many textures get consecutive, distinct slots
    TextureSlots many(1024);
    for (uint64_t i = 0; i < 1000; i++) CHECK(many.slotFor(i * 4096 + 17, 5) == static_cast<int32_t>(i + 1));
    CHECK(many.used() == 1001);
}

int main() {
    testTextureSlots();
    testRenderModes();
    testCapabilities();
    testGpuChoice();
    testTransformLayout();
    testAlignUp();
    testInstanceTableLayout();

    if (failures == 0) std::printf("RayTracingTest: all passed\n");
    else std::printf("RayTracingTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
