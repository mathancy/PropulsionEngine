#include "rendering/Mesh.h"
#include "rendering/VulkanContext.h"
#include "core/CrashHandler.h"
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <array>
#include <algorithm>

#ifdef _WIN32
#undef max
#undef min
#endif

namespace Forge {

VkVertexInputBindingDescription Vertex::getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);

    // Normal
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);

    // Texture coordinates
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoords);

    return attributeDescriptions;
}

Mesh::Mesh(VulkanContext* context, const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
    : m_context(context), m_vertices(vertices), m_indices(indices),
      m_vertexBuffer(VK_NULL_HANDLE), m_vertexBufferMemory(VK_NULL_HANDLE),
      m_indexBuffer(VK_NULL_HANDLE), m_indexBufferMemory(VK_NULL_HANDLE),
      m_boundsMin(0.0f), m_boundsMax(0.0f) {
    calculateBounds();
    createVertexBuffer();
    createIndexBuffer();
}

Mesh::~Mesh() {
    VkDevice device = m_context->getDevice();
    
    vkDestroyBuffer(device, m_indexBuffer, nullptr);
    vkFreeMemory(device, m_indexBufferMemory, nullptr);
    
    vkDestroyBuffer(device, m_vertexBuffer, nullptr);
    vkFreeMemory(device, m_vertexBufferMemory, nullptr);
}

void Mesh::calculateBounds() {
    if (m_vertices.empty()) {
        m_boundsMin = glm::vec3(0.0f);
        m_boundsMax = glm::vec3(0.0f);
        return;
    }
    
    m_boundsMin = m_vertices[0].position;
    m_boundsMax = m_vertices[0].position;
    
    for (const auto& vertex : m_vertices) {
        m_boundsMin = glm::min(m_boundsMin, vertex.position);
        m_boundsMax = glm::max(m_boundsMax, vertex.position);
    }
}

void Mesh::createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(m_vertices[0]) * m_vertices.size();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    
    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &stagingBufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create staging buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context->getDevice(), stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_context->findMemoryType(memRequirements.memoryTypeBits, 
                                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate staging buffer memory!");
    }

    vkBindBufferMemory(m_context->getDevice(), stagingBuffer, stagingBufferMemory, 0);

    // Copy vertex data to staging buffer
    void* data;
    vkMapMemory(m_context->getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_context->getDevice(), stagingBufferMemory);

    // Create vertex buffer in device local memory
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bufferInfo, nullptr, &m_vertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create vertex buffer!");
    }

    vkGetBufferMemoryRequirements(m_context->getDevice(), m_vertexBuffer, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_context->findMemoryType(memRequirements.memoryTypeBits, 
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &m_vertexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate vertex buffer memory!");
    }

    vkBindBufferMemory(m_context->getDevice(), m_vertexBuffer, m_vertexBufferMemory, 0);

    // Copy from staging buffer to vertex buffer
    VkCommandBuffer commandBuffer = m_context->beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_vertexBuffer, 1, &copyRegion);

    m_context->endSingleTimeCommands(commandBuffer);

    // Cleanup staging buffer
    vkDestroyBuffer(m_context->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_context->getDevice(), stagingBufferMemory, nullptr);
}

void Mesh::createIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(m_indices[0]) * m_indices.size();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    
    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &stagingBufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create staging buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context->getDevice(), stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_context->findMemoryType(memRequirements.memoryTypeBits, 
                                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate staging buffer memory!");
    }

    vkBindBufferMemory(m_context->getDevice(), stagingBuffer, stagingBufferMemory, 0);

    // Copy index data to staging buffer
    void* data;
    vkMapMemory(m_context->getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_indices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_context->getDevice(), stagingBufferMemory);

    // Create index buffer in device local memory
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bufferInfo, nullptr, &m_indexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create index buffer!");
    }

    vkGetBufferMemoryRequirements(m_context->getDevice(), m_indexBuffer, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_context->findMemoryType(memRequirements.memoryTypeBits, 
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &m_indexBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate index buffer memory!");
    }

    vkBindBufferMemory(m_context->getDevice(), m_indexBuffer, m_indexBufferMemory, 0);

    // Copy from staging buffer to index buffer
    VkCommandBuffer commandBuffer = m_context->beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_indexBuffer, 1, &copyRegion);

    m_context->endSingleTimeCommands(commandBuffer);

    // Cleanup staging buffer
    vkDestroyBuffer(m_context->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_context->getDevice(), stagingBufferMemory, nullptr);
}

void Mesh::draw(VkCommandBuffer commandBuffer) const {
    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);
}

void Mesh::drawPartial(VkCommandBuffer commandBuffer, uint32_t indexCount) const {
    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    // Clamp to actual index count
    uint32_t count = std::min(indexCount, static_cast<uint32_t>(m_indices.size()));
    vkCmdDrawIndexed(commandBuffer, count, 1, 0, 0, 0);
}

void Mesh::drawPartialRange(VkCommandBuffer commandBuffer, uint32_t firstIndex, uint32_t indexCount) const {
    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    // Clamp to actual index count
    uint32_t maxCount = static_cast<uint32_t>(m_indices.size());
    if (firstIndex >= maxCount) return;
    uint32_t count = std::min(indexCount, maxCount - firstIndex);
    vkCmdDrawIndexed(commandBuffer, count, 1, firstIndex, 0, 0);
}

Mesh* Mesh::createCube(VulkanContext* context) {
    std::vector<Vertex> vertices = {
        // Front face
        {{-0.5f, -0.5f,  0.5f}, {0.0f,  0.0f,  1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f,  0.0f,  1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f,  0.0f,  1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f,  0.0f,  1.0f}, {0.0f, 1.0f}},
        
        // Back face
        {{ 0.5f, -0.5f, -0.5f}, {0.0f,  0.0f, -1.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f,  0.0f, -1.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f,  0.0f, -1.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f,  0.0f, -1.0f}, {0.0f, 1.0f}},
        
        // Top face
        {{-0.5f,  0.5f,  0.5f}, {0.0f,  1.0f,  0.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f,  1.0f,  0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f,  1.0f,  0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f,  1.0f,  0.0f}, {0.0f, 1.0f}},
        
        // Bottom face
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f,  0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f,  0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f,  0.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f,  0.0f}, {0.0f, 1.0f}},
        
        // Right face
        {{ 0.5f, -0.5f,  0.5f}, {1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}},
        
        // Left face
        {{-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}}
    };

    std::vector<unsigned int> indices = {
        0, 1, 2, 2, 3, 0,       // Front
        4, 5, 6, 6, 7, 4,       // Back
        8, 9, 10, 10, 11, 8,    // Top
        12, 13, 14, 14, 15, 12, // Bottom
        16, 17, 18, 18, 19, 16, // Right
        20, 21, 22, 22, 23, 20  // Left
    };

    return new Mesh(context, vertices, indices);
}

Mesh* Mesh::createSphere(VulkanContext* context, int segments) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    
    const float PI = 3.14159265359f;
    const int rings = segments / 2;
    
    for (int ring = 0; ring <= rings; ring++) {
        float phi = PI * ring / rings;
        for (int seg = 0; seg <= segments; seg++) {
            float theta = 2.0f * PI * seg / segments;
            
            float x = sin(phi) * cos(theta);
            float y = sin(phi) * sin(theta);
            float z = cos(phi);
            
            vertices.push_back({
                {x * 0.5f, y * 0.5f, z * 0.5f},
                {x, y, z},
                {(float)seg / segments, (float)ring / rings}
            });
        }
    }
    
    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            int current = ring * (segments + 1) + seg;
            int next = current + segments + 1;
            
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);
            
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    return new Mesh(context, vertices, indices);
}

Mesh* Mesh::createCylinder(VulkanContext* context, int segments) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    
    const float PI = 3.14159265359f;
    const float height = 1.0f;
    const float radius = 0.5f;
    
    // Top cap center
    vertices.push_back({{0.0f, height / 2, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});
    
    // Top cap vertices
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * PI * i / segments;
        float x = radius * cos(theta);
        float z = radius * sin(theta);
        vertices.push_back({{x, height / 2, z}, {0.0f, 1.0f, 0.0f}, {(float)i / segments, 0.0f}});
    }
    
    // Side vertices (top ring)
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * PI * i / segments;
        float x = radius * cos(theta);
        float z = radius * sin(theta);
        float nx = cos(theta);
        float nz = sin(theta);
        vertices.push_back({{x, height / 2, z}, {nx, 0.0f, nz}, {(float)i / segments, 0.0f}});
    }
    
    // Side vertices (bottom ring)
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * PI * i / segments;
        float x = radius * cos(theta);
        float z = radius * sin(theta);
        float nx = cos(theta);
        float nz = sin(theta);
        vertices.push_back({{x, -height / 2, z}, {nx, 0.0f, nz}, {(float)i / segments, 1.0f}});
    }
    
    // Bottom cap vertices
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * PI * i / segments;
        float x = radius * cos(theta);
        float z = radius * sin(theta);
        vertices.push_back({{x, -height / 2, z}, {0.0f, -1.0f, 0.0f}, {(float)i / segments, 1.0f}});
    }
    
    // Bottom cap center
    vertices.push_back({{0.0f, -height / 2, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f}});
    
    // Top cap indices
    for (int i = 0; i < segments; i++) {
        indices.push_back(0);
        indices.push_back(i + 1);
        indices.push_back(i + 2);
    }
    
    // Side indices
    int sideStart = segments + 2;
    for (int i = 0; i < segments; i++) {
        int top1 = sideStart + i;
        int top2 = sideStart + i + 1;
        int bot1 = sideStart + segments + 1 + i;
        int bot2 = sideStart + segments + 1 + i + 1;
        
        indices.push_back(top1);
        indices.push_back(bot1);
        indices.push_back(top2);
        
        indices.push_back(top2);
        indices.push_back(bot1);
        indices.push_back(bot2);
    }
    
    // Bottom cap indices
    int bottomCapStart = sideStart + 2 * (segments + 1);
    int bottomCenter = bottomCapStart + segments + 1;
    for (int i = 0; i < segments; i++) {
        indices.push_back(bottomCenter);
        indices.push_back(bottomCapStart + i + 1);
        indices.push_back(bottomCapStart + i);
    }
    
    return new Mesh(context, vertices, indices);
}

Mesh* Mesh::createPlane(VulkanContext* context) {
    std::vector<Vertex> vertices = {
        {{-0.5f, 0.0f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, 0.0f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, 0.0f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.0f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}
    };
    
    std::vector<unsigned int> indices = {
        0, 1, 2, 2, 3, 0
    };
    
    return new Mesh(context, vertices, indices);
}

Mesh* Mesh::createCone(VulkanContext* context, int segments) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    
    const float PI = 3.14159265359f;
    const float height = 1.0f;
    const float radius = 0.5f;
    
    // Apex
    vertices.push_back({{0.0f, height / 2, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.0f}});
    
    // Side vertices
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * PI * i / segments;
        float x = radius * cos(theta);
        float z = radius * sin(theta);
        
        glm::vec3 normal = glm::normalize(glm::vec3(x, height / 2, z));
        vertices.push_back({{x, -height / 2, z}, normal, {(float)i / segments, 1.0f}});
    }
    
    // Base center
    vertices.push_back({{0.0f, -height / 2, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 1.0f}});
    
    // Side triangles
    for (int i = 0; i < segments; i++) {
        indices.push_back(0);
        indices.push_back(i + 1);
        indices.push_back(i + 2);
    }
    
    // Base triangles
    int baseCenter = segments + 2;
    for (int i = 0; i < segments; i++) {
        indices.push_back(baseCenter);
        indices.push_back(i + 2);
        indices.push_back(i + 1);
    }
    
    return new Mesh(context, vertices, indices);
}

Mesh* Mesh::createTorus(VulkanContext* context, float majorRadius, float minorRadius, int majorSegments, int minorSegments) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    
    const float PI = 3.14159265359f;
    
    for (int i = 0; i <= majorSegments; i++) {
        float u = 2.0f * PI * i / majorSegments;
        float cu = cos(u);
        float su = sin(u);
        
        for (int j = 0; j <= minorSegments; j++) {
            float v = 2.0f * PI * j / minorSegments;
            float cv = cos(v);
            float sv = sin(v);
            
            float x = (majorRadius + minorRadius * cv) * cu;
            float y = minorRadius * sv;
            float z = (majorRadius + minorRadius * cv) * su;
            
            glm::vec3 center(majorRadius * cu, 0.0f, majorRadius * su);
            glm::vec3 position(x, y, z);
            glm::vec3 normal = glm::normalize(position - center);
            
            vertices.push_back({
                {x, y, z},
                normal,
                {(float)i / majorSegments, (float)j / minorSegments}
            });
        }
    }
    
    for (int i = 0; i < majorSegments; i++) {
        for (int j = 0; j < minorSegments; j++) {
            int current = i * (minorSegments + 1) + j;
            int next = current + minorSegments + 1;
            
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);
            
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    return new Mesh(context, vertices, indices);
}

Mesh* Mesh::createDisc(VulkanContext* context, int segments) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    
    const float PI = 3.14159265359f;
    
    // ========== TOP SIDE (normal pointing up) ==========
    // Center vertex
    vertices.push_back({
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},  // Normal pointing up (Y-up for disc in XZ plane)
        {0.5f, 0.5f}
    });
    
    // Edge vertices around the circumference
    for (int i = 0; i <= segments; i++) {
        float angle = 2.0f * PI * i / segments;
        float x = cos(angle);
        float z = sin(angle);
        
        vertices.push_back({
            {x, 0.0f, z},
            {0.0f, 1.0f, 0.0f},
            {(x + 1.0f) * 0.5f, (z + 1.0f) * 0.5f}
        });
    }
    
    // ========== BOTTOM SIDE (normal pointing down) ==========
    int bottomCenterIdx = static_cast<int>(vertices.size());
    
    // Center vertex (bottom)
    vertices.push_back({
        {0.0f, 0.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},  // Normal pointing down
        {0.5f, 0.5f}
    });
    
    // Edge vertices around the circumference (bottom)
    for (int i = 0; i <= segments; i++) {
        float angle = 2.0f * PI * i / segments;
        float x = cos(angle);
        float z = sin(angle);
        
        vertices.push_back({
            {x, 0.0f, z},
            {0.0f, -1.0f, 0.0f},
            {(x + 1.0f) * 0.5f, (z + 1.0f) * 0.5f}
        });
    }
    
    // Create triangles - TOP SIDE (counter-clockwise when viewed from above)
    for (int i = 1; i <= segments; i++) {
        indices.push_back(0);        // Center
        indices.push_back(i);        // Current edge vertex
        indices.push_back(i + 1);    // Next edge vertex
    }
    
    // Create triangles - BOTTOM SIDE (reversed winding for back face)
    for (int i = 1; i <= segments; i++) {
        indices.push_back(bottomCenterIdx);           // Center
        indices.push_back(bottomCenterIdx + i + 1);   // Next edge vertex (swapped)
        indices.push_back(bottomCenterIdx + i);       // Current edge vertex (swapped)
    }
    
    Mesh* mesh = new Mesh(context, vertices, indices);
    mesh->setSegmentCount(segments);  // Still tracks segments for partial drawing (uses top side)
    return mesh;
}

Mesh* Mesh::createWedge(VulkanContext* context, float startAngle, float endAngle, int segments) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    
    CrashHandler::logInfo("createWedge: startAngle=" + std::to_string(startAngle) + ", endAngle=" + std::to_string(endAngle) + ", segments=" + std::to_string(segments));
    
    // Handle the case where there's no wedge to draw
    float angleDiff = endAngle - startAngle;
    CrashHandler::logInfo("createWedge: angleDiff=" + std::to_string(angleDiff));
    
    if (std::abs(angleDiff) < 0.001f) {
        CrashHandler::logInfo("createWedge: Creating degenerate mesh (angleDiff too small)");
        // Return a minimal valid mesh (degenerate triangle)
        vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});
        vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});
        vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});
        indices.push_back(0);
        indices.push_back(1);
        indices.push_back(2);
        return new Mesh(context, vertices, indices);
    }
    
    // Center vertex
    vertices.push_back({
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.5f, 0.5f}
    });
    
    // Calculate how many segments we need based on the angle
    float absAngleDiff = std::abs(angleDiff);
    int numSegments = std::max(3, static_cast<int>(absAngleDiff / (2.0f * 3.14159265359f) * segments));
    CrashHandler::logInfo("createWedge: numSegments=" + std::to_string(numSegments));
    
    // Always iterate from start to end, the sign of angleDiff handles direction
    for (int i = 0; i <= numSegments; i++) {
        float t = static_cast<float>(i) / numSegments;
        float angle = startAngle + t * angleDiff;
        float x = std::cos(angle);
        float z = std::sin(angle);
        
        vertices.push_back({
            {x, 0.0f, z},
            {0.0f, 1.0f, 0.0f},
            {(x + 1.0f) * 0.5f, (z + 1.0f) * 0.5f}
        });
    }
    
    CrashHandler::logInfo("createWedge: vertices count=" + std::to_string(vertices.size()));
    
    // Create triangles from center to each edge segment
    // Swap winding order based on direction to ensure both sides render
    if (angleDiff > 0) {
        for (int i = 1; i <= numSegments; i++) {
            indices.push_back(0);        // Center
            indices.push_back(i);        // Current edge vertex
            indices.push_back(i + 1);    // Next edge vertex
        }
    } else {
        // Reverse winding for negative angles
        for (int i = 1; i <= numSegments; i++) {
            indices.push_back(0);        // Center
            indices.push_back(i + 1);    // Next edge vertex
            indices.push_back(i);        // Current edge vertex
        }
    }
    
    CrashHandler::logInfo("createWedge: indices count=" + std::to_string(indices.size()) + ", creating mesh...");
    
    return new Mesh(context, vertices, indices);
}

} // namespace Forge

