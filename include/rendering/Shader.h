#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace Forge {

class VulkanContext;

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
    alignas(16) glm::vec4 colorAndExposure;   // RGB = color, A = exposure
    alignas(16) glm::vec4 materialParams;     // x=metallic, y=roughness, z=ao, w=unused
    alignas(16) glm::vec4 textureFlags;       // x=useAlbedo, y=useMetallic, z=useRoughness, w=useNormal
    alignas(16) glm::vec4 textureFlags2;      // x=useAO, y=useEmissive, z=useHeight, w=unused
    alignas(16) glm::vec4 invertFlags;        // x=invertMetallic, y=invertRoughness, z=invertNormal, w=invertAO
    alignas(16) glm::vec4 textureStrengths;   // x=metallicStr, y=roughnessStr, z=normalStr, w=aoStr
    alignas(16) glm::vec4 emissiveColor;      // RGB = emissive color, A = emissive texture strength
    alignas(16) glm::vec4 heightParams;       // x=heightScale, y=invertHeight, z=unused, w=unused
    alignas(16) glm::vec3 cameraPos;          // Camera position for specular calculations
    alignas(4) float padding;
};

// Push constants for gizmo rendering (model + color change per draw)
struct GizmoPushConstants {
    alignas(16) glm::mat4 model;
    alignas(16) glm::vec4 color;
};

// Smaller UBO for gizmo (only view/proj, which stay constant per frame)
struct GizmoUniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
};

class Shader {
public:
    // Maximum number of objects that can be rendered per frame
    static constexpr uint32_t MAX_OBJECTS = 256;
    
    // Standard shader (no push constants)
    Shader(VulkanContext* context, const char* vertexPath, const char* fragmentPath, VkRenderPass renderPass = VK_NULL_HANDLE, VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT, bool depthTestEnable = true, bool supportsTextures = false);
    
    // Gizmo shader with push constants, MSAA support, and alpha blending
    static std::unique_ptr<Shader> createGizmoShader(VulkanContext* context, const char* vertexPath, const char* fragmentPath, VkRenderPass renderPass, VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
    
    ~Shader();

    void bind(VkCommandBuffer commandBuffer);
    
    // Dynamic uniform buffer methods for multi-object rendering
    void updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo);
    void updateUniformBufferDynamic(uint32_t currentFrame, const UniformBufferObject& ubo);
    void resetObjectIndex();
    void bindWithDynamicOffset(VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t objectIndex);
    uint32_t getDynamicAlignment() const { return m_dynamicAlignment; }
    
    void updateGizmoUniformBuffer(uint32_t currentFrame, const GizmoUniformBufferObject& ubo);
    void pushConstants(VkCommandBuffer commandBuffer, const GizmoPushConstants& constants);
    
    // PBR texture binding (binding indices: 1=albedo, 2=metallic, 3=roughness, 4=normal, 5=ao, 6=emissive)
    void bindTexture(uint32_t currentFrame, VkImageView textureView, VkSampler sampler, uint32_t binding = 1);
    void bindPBRTextures(uint32_t currentFrame, 
                         VkImageView albedoView, VkSampler albedoSampler,
                         VkImageView metallicView, VkSampler metallicSampler,
                         VkImageView roughnessView, VkSampler roughnessSampler,
                         VkImageView normalView, VkSampler normalSampler,
                         VkImageView aoView, VkSampler aoSampler,
                         VkImageView emissiveView, VkSampler emissiveSampler);
    void unbindTexture(uint32_t currentFrame);
    void unbindAllTextures(uint32_t currentFrame);
    
    bool usesPushConstants() const { return m_usesPushConstants; }
    bool supportsTextures() const { return m_supportsTextures; }
    
    VkPipeline getPipeline() const { return m_pipeline; }
    VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }
    VkDescriptorSet getDescriptorSet(uint32_t frame) const { return m_descriptorSets[frame]; }

private:
    // Private constructor for gizmo shader with MSAA support
    Shader(VulkanContext* context, const char* vertexPath, const char* fragmentPath, VkRenderPass renderPass, bool usesPushConstants, VkSampleCountFlagBits msaaSamples);
    
    void createDescriptorSetLayout();
    void createTexturedDescriptorSetLayout();
    void createGizmoDescriptorSetLayout();
    void createGraphicsPipeline(const std::string& vertexPath, const std::string& fragmentPath, VkRenderPass renderPass);
    void createGizmoPipeline(const std::string& vertexPath, const std::string& fragmentPath, VkRenderPass renderPass);
    void createUniformBuffers();
    void createGizmoUniformBuffers();
    void createDescriptorSets();
    void createTexturedDescriptorSets();
    void createGizmoDescriptorSets();
    void createDefaultTexture();
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);

    VulkanContext* m_context;
    VkPipeline m_pipeline;
    VkPipelineLayout m_pipelineLayout;
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkCullModeFlags m_cullMode;
    bool m_depthTestEnable;
    bool m_usesPushConstants = false;
    bool m_supportsTextures = false;
    VkSampleCountFlagBits m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    
    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<void*> m_uniformBuffersMapped;
    std::vector<VkDescriptorSet> m_descriptorSets;
    
    // Dynamic uniform buffer alignment (query from device)
    uint32_t m_dynamicAlignment = 0;
    uint32_t m_objectIndex = 0;
    VkDeviceSize m_dynamicBufferSize = 0;
    
    // Default white texture for when no texture is bound
    VkImage m_defaultTexture = VK_NULL_HANDLE;
    VkDeviceMemory m_defaultTextureMemory = VK_NULL_HANDLE;
    VkImageView m_defaultTextureView = VK_NULL_HANDLE;
    VkSampler m_defaultSampler = VK_NULL_HANDLE;
};

} // namespace Forge