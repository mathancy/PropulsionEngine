#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace Propulsion {

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
    alignas(16) glm::ivec4 textureSlots;      // places in the texture table: x=base colour, y=metallic, z=roughness, w=normal (-1: none)
    alignas(16) glm::ivec4 textureSlots2;     // x=occlusion, y=emissive, z=height, w=opacity (-1: none)
    alignas(16) glm::vec4 surfaceParams;      // x=blend mode (0 opaque, 1 masked, 2 translucent), y=opacity, z=mask cutoff, w=1 if two sided
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
    
    // Standard shader (no push constants). `extraSetLayout`, if given, is a second descriptor set (set 1) that the shader reads
    // and the draw code binds itself: the scene's lights and environment (see SceneLighting).
    // `bindlessSetLayout`, if given (it needs `extraSetLayout` too), is a third descriptor set (set 2): the table of every material texture
    // (see TextureTable). A shader with it also draws with a cull mode chosen per draw (`setCullMode`), so two-sided materials can show their backs.
    Shader(VulkanContext* context, const char* vertexPath, const char* fragmentPath, VkRenderPass renderPass = VK_NULL_HANDLE, VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT, bool depthTestEnable = true, bool supportsTextures = false, VkDescriptorSetLayout extraSetLayout = VK_NULL_HANDLE, VkDescriptorSetLayout bindlessSetLayout = VK_NULL_HANDLE);
    
    // Gizmo shader with push constants, MSAA support, and alpha blending
    static std::unique_ptr<Shader> createGizmoShader(VulkanContext* context, const char* vertexPath, const char* fragmentPath, VkRenderPass renderPass, VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
    
    ~Shader();

    // `variant` is a pipeline added by addVariant (0, or one that doesn't exist, is the normal one).
    void bind(VkCommandBuffer commandBuffer, int variant = 0);

    // Another pipeline for the same shader inputs: same vertex shader, state, descriptor sets and layout, a different fragment shader
    // and/or alpha blending (colour already multiplied by alpha, depth not written: for see-through surfaces). Returns its number for `bind`,
    // or 0 if it couldn't be made. Used for the ray-traced version of the scene shader (shaders/basic_rt_frag.spv) and for translucent draws.
    int addVariant(const std::string& fragmentPath, VkRenderPass renderPass, bool blended);
    bool hasVariant(int variant) const { return variant > 0 && variant <= static_cast<int>(m_variants.size()); }

    // A shader made with a bindless set: the faces to cull, for the draws after this call (two-sided draws cull none).
    void setCullMode(VkCommandBuffer commandBuffer, bool twoSided) const;
    
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
    VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }  // set 0: the per-object uniforms and textures
    VkDescriptorSet getDescriptorSet(uint32_t frame) const { return m_descriptorSets[frame]; }

private:
    // Private constructor for gizmo shader with MSAA support
    Shader(VulkanContext* context, const char* vertexPath, const char* fragmentPath, VkRenderPass renderPass, bool usesPushConstants, VkSampleCountFlagBits msaaSamples);
    
    void createDescriptorSetLayout();
    void createTexturedDescriptorSetLayout();
    void createGizmoDescriptorSetLayout();
    // Builds the pipeline (and, unless `variantOut` is given, the pipeline layout it uses). With `variantOut` it makes another
    // pipeline for the existing layout and stores it there.
    void createGraphicsPipeline(const std::string& vertexPath, const std::string& fragmentPath, VkRenderPass renderPass, VkPipeline* variantOut = nullptr, bool blended = false);
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
    std::vector<VkPipeline> m_variants;  // variant n is m_variants[n - 1]
    std::string m_vertexPath;
    VkDescriptorSetLayout m_bindlessSetLayout = VK_NULL_HANDLE;  // not owned
    bool m_dynamicCull = false;
    VkPipelineLayout m_pipelineLayout;
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSetLayout m_extraSetLayout = VK_NULL_HANDLE;  // not owned
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

} // namespace Propulsion