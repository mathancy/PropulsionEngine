#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>

namespace Propulsion {

class Shader;
class Camera;
class VulkanContext;

class WorldGrid {
public:
    WorldGrid(VulkanContext* vulkanContext);
    ~WorldGrid();
    
    void render(VkCommandBuffer commandBuffer, const Camera* camera, float aspectRatio);
    void render(VkCommandBuffer commandBuffer, const glm::mat4& view, const glm::mat4& projection, 
                const glm::vec3& cameraPos, float fov = 45.0f);
    
    void setGridSize(float size) { m_gridSize = size; }
    void setGridSpacing(float spacing) { m_gridSpacing = spacing; }
    void setFadeDistance(float distance) { m_fadeDistance = distance; }
    void setGridColor(const glm::vec3& color) { m_gridColor = color; }
    void setAxisXColor(const glm::vec3& color) { m_axisXColor = color; }
    void setAxisYColor(const glm::vec3& color) { m_axisYColor = color; }
    
private:
    void initializeGrid();
    void createVertexBuffer();
    void createUniformBuffers();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createPipeline();
    void cleanup();
    
    VulkanContext* m_vulkanContext;
    
    // Vulkan resources
    VkBuffer m_vertexBuffer;
    VkDeviceMemory m_vertexBufferMemory;
    
    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<void*> m_uniformBuffersMapped;
    
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorPool m_descriptorPool;
    std::vector<VkDescriptorSet> m_descriptorSets;
    
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_pipeline;
    VkShaderModule m_vertShaderModule;
    VkShaderModule m_fragShaderModule;
    
    // Grid settings
    float m_gridSize;
    float m_gridSpacing;
    float m_fadeDistance;
    glm::vec3 m_gridColor;
    glm::vec3 m_axisXColor;
    glm::vec3 m_axisYColor;
};

} // namespace Propulsion

