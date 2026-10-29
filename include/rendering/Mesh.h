#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace Propulsion {

class VulkanContext;

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    
    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
};

class Mesh {
public:
    Mesh(VulkanContext* context, const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
    ~Mesh();

    void draw(VkCommandBuffer commandBuffer) const;
    void drawPartial(VkCommandBuffer commandBuffer, uint32_t indexCount) const;
    void drawPartialRange(VkCommandBuffer commandBuffer, uint32_t firstIndex, uint32_t indexCount) const;
    
    static Mesh* createCube(VulkanContext* context);
    static Mesh* createSphere(VulkanContext* context, int segments = 32);
    static Mesh* createCylinder(VulkanContext* context, int segments = 32);
    static Mesh* createPlane(VulkanContext* context);
    static Mesh* createCone(VulkanContext* context, int segments = 16);
    static Mesh* createTorus(VulkanContext* context, float majorRadius, float minorRadius, int majorSegments = 32, int minorSegments = 16);
    static Mesh* createDisc(VulkanContext* context, int segments = 48);
    static Mesh* createWedge(VulkanContext* context, float startAngle, float endAngle, int segments = 48);
    
    size_t getIndexCount() const { return m_indices.size(); }
    int getSegmentCount() const { return m_segmentCount; }
    const std::vector<Vertex>& getVertices() const { return m_vertices; }
    const std::vector<unsigned int>& getIndices() const { return m_indices; }
    
    // Bounding box accessors (local space, before any transforms)
    glm::vec3 getBoundsMin() const { return m_boundsMin; }
    glm::vec3 getBoundsMax() const { return m_boundsMax; }
    glm::vec3 getBoundsCenter() const { return (m_boundsMin + m_boundsMax) * 0.5f; }
    glm::vec3 getBoundsExtents() const { return (m_boundsMax - m_boundsMin) * 0.5f; }
    
    void setSegmentCount(int segments) { m_segmentCount = segments; }

    // Ray tracing: where the vertex and index data live (zero when the GPU can't do ray queries), and the bottom-level
    // acceleration structure built from them (see RayTracing.h, which builds it the first time the mesh is traced).
    VkDeviceAddress getVertexAddress() const { return m_vertexAddress; }
    VkDeviceAddress getIndexAddress() const { return m_indexAddress; }
    uint32_t getVertexCount() const { return static_cast<uint32_t>(m_vertices.size()); }
    VkDeviceAddress getBlasAddress() const { return m_blas.address; }
    bool hasBlas() const { return m_blas.handle != VK_NULL_HANDLE; }
    struct Blas {
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceAddress address = 0;
    };
    void setBlas(const Blas& blas) { destroyBlas(); m_blas = blas; }

private:
    void createVertexBuffer();
    void createIndexBuffer();
    void calculateBounds();
    void destroyBlas();

    VulkanContext* m_context;
    std::vector<Vertex> m_vertices;
    std::vector<unsigned int> m_indices;
    int m_segmentCount = 0;  // For disc meshes, tracks the number of segments
    
    // Bounding box in local space
    glm::vec3 m_boundsMin;
    glm::vec3 m_boundsMax;
    
    VkBuffer m_vertexBuffer;
    VkDeviceMemory m_vertexBufferMemory;
    VkBuffer m_indexBuffer;
    VkDeviceMemory m_indexBufferMemory;
    VkDeviceAddress m_vertexAddress = 0;
    VkDeviceAddress m_indexAddress = 0;
    Blas m_blas;
};

} // namespace Propulsion
