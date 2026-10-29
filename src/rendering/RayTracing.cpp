#include "rendering/RayTracing.h"
#include "rendering/VulkanContext.h"
#include "rendering/RayTracingSupport.h"
#include "core/CrashHandler.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Propulsion {

static_assert(VulkanContext::MAX_FRAMES_IN_FLIGHT == 2, "RayTracing keeps one set of buffers per frame in flight");

namespace {
constexpr uint32_t kMinInstanceCapacity = 16;

uint32_t roundUpToPowerOfTwo(uint32_t value) {
    uint32_t result = 1;
    while (result < value) result <<= 1;
    return result;
}
}

RayTracing::RayTracing(VulkanContext* context) : m_context(context) {
    m_available = context->getRayTracingCaps().canQuery();
    CrashHandler::logInfo(m_available ? "Ray tracing scene manager ready" : "Ray tracing scene manager off (no ray query support)");
}

RayTracing::~RayTracing() {
    for (FrameData& frame : m_frames) destroyFrame(frame);
}

void RayTracing::createBuffer(Buffer& out, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory, bool map) {
    out = Buffer{};
    out.size = size;
    m_context->createBuffer(size, usage, memory, out.buffer, out.memory, true);
    out.address = m_context->getBufferAddress(out.buffer);
    if (map && vkMapMemory(m_context->getDevice(), out.memory, 0, size, 0, &out.mapped) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map ray tracing buffer!");
    }
}

void RayTracing::destroyBuffer(Buffer& buffer) {
    if (buffer.mapped) vkUnmapMemory(m_context->getDevice(), buffer.memory);
    if (buffer.buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_context->getDevice(), buffer.buffer, nullptr);
    if (buffer.memory != VK_NULL_HANDLE) vkFreeMemory(m_context->getDevice(), buffer.memory, nullptr);
    buffer = Buffer{};
}

void RayTracing::destroyFrame(FrameData& frame) {
    if (frame.tlas != VK_NULL_HANDLE) {
        m_context->getRayTracingFunctions().vkDestroyAccelerationStructureKHR(m_context->getDevice(), frame.tlas, nullptr);
        frame.tlas = VK_NULL_HANDLE;
    }
    destroyBuffer(frame.instances);
    destroyBuffer(frame.table);
    destroyBuffer(frame.storage);
    destroyBuffer(frame.scratch);
    frame.capacity = 0;
    frame.count = 0;
}

bool RayTracing::ensureBlas(Mesh& mesh) {
    if (mesh.hasBlas()) return true;
    if (mesh.getIndexCount() < 3 || mesh.getVertexAddress() == 0 || mesh.getIndexAddress() == 0) return false;

    const RayTracingFunctions& fn = m_context->getRayTracingFunctions();
    VkDevice device = m_context->getDevice();

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;  // position is the first member of Vertex
    geometry.geometry.triangles.vertexData.deviceAddress = mesh.getVertexAddress();
    geometry.geometry.triangles.vertexStride = sizeof(Vertex);
    geometry.geometry.triangles.maxVertex = mesh.getVertexCount() - 1;
    geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    geometry.geometry.triangles.indexData.deviceAddress = mesh.getIndexAddress();

    const uint32_t triangleCount = static_cast<uint32_t>(mesh.getIndexCount() / 3);

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    fn.vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &triangleCount, &sizes);

    Mesh::Blas blas;
    m_context->createBuffer(sizes.accelerationStructureSize,
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, blas.buffer, blas.memory, true);

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = blas.buffer;
    createInfo.size = sizes.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (fn.vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &blas.handle) != VK_SUCCESS) {
        vkDestroyBuffer(device, blas.buffer, nullptr);
        vkFreeMemory(device, blas.memory, nullptr);
        CrashHandler::logWarning("Could not create a bottom-level acceleration structure");
        return false;
    }

    // The scratch address must be aligned, so ask for a little more and round the address up.
    const uint64_t alignment = m_context->getAccelerationStructureScratchAlignment();
    Buffer scratch;
    createBuffer(scratch, sizes.buildScratchSize + alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);

    buildInfo.dstAccelerationStructure = blas.handle;
    buildInfo.scratchData.deviceAddress = alignUp(scratch.address, alignment);

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = triangleCount;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;

    VkCommandBuffer commandBuffer = m_context->beginSingleTimeCommands();
    fn.vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &ranges);
    m_context->endSingleTimeCommands(commandBuffer);  // waits for the queue, so the scratch can go now
    destroyBuffer(scratch);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = blas.handle;
    blas.address = fn.vkGetAccelerationStructureDeviceAddressKHR(device, &addressInfo);

    mesh.setBlas(blas);
    return true;
}

void RayTracing::growFrame(FrameData& frame, uint32_t instanceCount) {
    const RayTracingFunctions& fn = m_context->getRayTracingFunctions();
    VkDevice device = m_context->getDevice();

    destroyFrame(frame);
    frame.capacity = (std::max)(kMinInstanceCapacity, roundUpToPowerOfTwo(instanceCount));

    createBuffer(frame.instances, sizeof(VkAccelerationStructureInstanceKHR) * frame.capacity,
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    createBuffer(frame.table, sizeof(GpuRtInstance) * frame.capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = frame.instances.address;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    const uint32_t maxInstances = frame.capacity;
    fn.vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxInstances, &sizes);

    createBuffer(frame.storage, sizes.accelerationStructureSize,
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
    createBuffer(frame.scratch, sizes.buildScratchSize + m_context->getAccelerationStructureScratchAlignment(),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = frame.storage.buffer;
    createInfo.size = sizes.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (fn.vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &frame.tlas) != VK_SUCCESS) {
        destroyFrame(frame);
        throw std::runtime_error("Failed to create the top-level acceleration structure!");
    }
}

void RayTracing::update(VkCommandBuffer commandBuffer, uint32_t frameIndex, const std::vector<Instance>& instances) {
    if (!m_available) return;
    FrameData& frame = m_frames[frameIndex];

    std::vector<VkAccelerationStructureInstanceKHR> structureInstances;
    std::vector<GpuRtInstance> table;
    structureInstances.reserve(instances.size());
    table.reserve(instances.size());
    for (const Instance& instance : instances) {
        if (!instance.mesh || !ensureBlas(*instance.mesh)) continue;

        VkAccelerationStructureInstanceKHR placed{};
        const std::array<float, 12> rows = rowMajor3x4(instance.model);
        std::memcpy(&placed.transform, rows.data(), sizeof(rows));
        placed.instanceCustomIndex = static_cast<uint32_t>(table.size());
        placed.mask = 0xFF;
        placed.instanceShaderBindingTableRecordOffset = 0;
        placed.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        placed.accelerationStructureReference = instance.mesh->getBlasAddress();
        structureInstances.push_back(placed);

        GpuRtInstance entry{};
        entry.vertexAddress = instance.mesh->getVertexAddress();
        entry.indexAddress = instance.mesh->getIndexAddress();
        entry.normalMatrix = glm::inverseTranspose(glm::mat3(instance.model));
        entry.materialIndex = instance.materialIndex;
        table.push_back(entry);
    }

    const uint32_t count = static_cast<uint32_t>(structureInstances.size());
    if (frame.tlas == VK_NULL_HANDLE || count > frame.capacity) growFrame(frame, count);
    frame.count = count;

    if (count > 0) {
        std::memcpy(frame.instances.mapped, structureInstances.data(), sizeof(VkAccelerationStructureInstanceKHR) * count);
        std::memcpy(frame.table.mapped, table.data(), sizeof(GpuRtInstance) * count);
    }

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = frame.instances.address;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.dstAccelerationStructure = frame.tlas;
    buildInfo.scratchData.deviceAddress = alignUp(frame.scratch.address, m_context->getAccelerationStructureScratchAlignment());

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = count;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    m_context->getRayTracingFunctions().vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &ranges);

    // The structure is read by shaders (ray queries) after it is built; the instance table was written by the host.
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

} // namespace Propulsion
