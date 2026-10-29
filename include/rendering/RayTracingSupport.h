#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

// Pure helpers for the ray-tracing foundation (no Vulkan calls, so tests can run them).
namespace Propulsion {

/// What the chosen GPU and driver can do for ray tracing. `canQuery` is what Hybrid RT needs (ray queries from compute and
/// fragment shaders); `canTracePipeline` is what the path tracer needs (ray-generation / closest-hit / miss shaders).
struct RayTracingCaps {
    bool accelerationStructure = false;  // VK_KHR_acceleration_structure + its feature bit
    bool rayQuery = false;               // VK_KHR_ray_query + its feature bit
    bool rayTracingPipeline = false;     // VK_KHR_ray_tracing_pipeline + its feature bit
    bool deferredHostOperations = false; // VK_KHR_deferred_host_operations (required by the extension above)
    bool bufferDeviceAddress = false;    // Vulkan 1.2
    bool descriptorIndexing = false;     // the bindless texture table: runtime arrays, non-uniform indexing, partially bound
    bool scalarBlockLayout = false;      // Vulkan 1.2
    bool shaderInt64 = false;            // 64-bit addresses in shaders

    bool canQuery() const {
        return accelerationStructure && rayQuery && bufferDeviceAddress && descriptorIndexing && scalarBlockLayout && shaderInt64;
    }
    bool canTracePipeline() const { return canQuery() && rayTracingPipeline && deferredHostOperations; }
};

/// What we know about a GPU when choosing between them.
struct GpuCandidate {
    bool suitable = false;     // has the queues, swap chain and features the editor needs at all
    bool discrete = false;     // a discrete card rather than integrated graphics
    bool canQuery = false;     // see RayTracingCaps
    uint64_t vramBytes = 0;    // the biggest device-local heap
};

/// True when `a` should be used in preference to `b`: it must be suitable; then ray-query support wins (the ray-traced
/// modes need it), then a discrete card, then more memory. Ties keep the earlier one (`a` is the incumbent).
inline bool gpuIsBetter(const GpuCandidate& a, const GpuCandidate& b) {
    if (a.suitable != b.suitable) return a.suitable;
    if (a.canQuery != b.canQuery) return a.canQuery;
    if (a.discrete != b.discrete) return a.discrete;
    return a.vramBytes > b.vramBytes;
}

/// A transform in the layout the acceleration structure builder wants: three rows of four, row-major. glm stores columns.
inline std::array<float, 12> rowMajor3x4(const glm::mat4& m) {
    std::array<float, 12> rows{};
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) rows[row * 4 + column] = m[column][row];
    }
    return rows;
}

/// Rounds `value` up to a multiple of `alignment` (zero means "no alignment").
inline uint64_t alignUp(uint64_t value, uint64_t alignment) {
    return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
}

} // namespace Propulsion
