#pragma once

#include "rendering/Mesh.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace Propulsion {

class VulkanContext;

/// What a shader reads about each instance in the top-level structure. The instance's custom index is its position in
/// this table. Scalar block layout (the GLSL side is in shaders/raytracing.glsl, keep the two in step).
struct GpuRtInstance {
    uint64_t vertexAddress;    // the mesh's Vertex array (position, normal, uv: 32 bytes each)
    uint64_t indexAddress;     // the mesh's 32-bit indices
    glm::mat3 normalMatrix;    // object normals to world normals (column-major, 9 floats)
    uint32_t materialIndex;    // reserved for the material table
};
static_assert(sizeof(GpuRtInstance) == 56, "GpuRtInstance must match the scalar-layout struct in raytracing.glsl");
static_assert(offsetof(GpuRtInstance, indexAddress) == 8 && offsetof(GpuRtInstance, normalMatrix) == 16 &&
              offsetof(GpuRtInstance, materialIndex) == 52, "GpuRtInstance layout");

/**
 * RayTracing
 * The scene as the GPU's ray tracer sees it: one bottom-level acceleration structure (BLAS) per mesh, built the first
 * time the mesh is traced, and one top-level structure (TLAS) per frame in flight, rebuilt every frame from the visible
 * objects' transforms. `update` also fills the instance table the shaders use to find a hit's vertices.
 *
 * Only exists when the GPU can do ray queries (VulkanContext::getRayTracingCaps().canQuery()); check `available()`.
 * Everything here is for Raster-independent passes: the raster path never touches it.
 */
class RayTracing {
public:
    struct Instance {
        Mesh* mesh = nullptr;
        glm::mat4 model{1.0f};
        uint32_t materialIndex = 0;
    };

    explicit RayTracing(VulkanContext* context);
    ~RayTracing();

    RayTracing(const RayTracing&) = delete;
    RayTracing& operator=(const RayTracing&) = delete;

    bool available() const { return m_available; }

    /// Builds any missing BLAS (waiting for the GPU, once per mesh), then records this frame's TLAS build and the instance
    /// table upload into `commandBuffer`. Meshes with no triangles are skipped. Call before any pass that traces rays.
    void update(VkCommandBuffer commandBuffer, uint32_t frame, const std::vector<Instance>& instances);

    VkAccelerationStructureKHR getTlas(uint32_t frame) const { return m_frames[frame].tlas; }
    VkBuffer getInstanceTable(uint32_t frame) const { return m_frames[frame].table.buffer; }
    VkDeviceSize getInstanceTableSize(uint32_t frame) const { return m_frames[frame].table.size; }
    /// How many instances the last `update` for this frame put in the TLAS.
    uint32_t getInstanceCount(uint32_t frame) const { return m_frames[frame].count; }

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        VkDeviceAddress address = 0;
        void* mapped = nullptr;
    };
    struct FrameData {
        Buffer instances;   // VkAccelerationStructureInstanceKHR, written by the CPU
        Buffer table;       // GpuRtInstance, written by the CPU, read by shaders
        Buffer storage;     // the TLAS itself
        Buffer scratch;
        VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
        uint32_t capacity = 0;  // instances the buffers above have room for
        uint32_t count = 0;
    };

    bool ensureBlas(Mesh& mesh);
    void createBuffer(Buffer& out, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory, bool map);
    void destroyBuffer(Buffer& buffer);
    void growFrame(FrameData& frame, uint32_t instanceCount);
    void destroyFrame(FrameData& frame);

    VulkanContext* m_context;
    bool m_available = false;
    std::array<FrameData, 2> m_frames;
};

} // namespace Propulsion
