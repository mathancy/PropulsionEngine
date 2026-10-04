#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <string>

namespace Forge {

class VulkanContext;
class Mesh;

/**
 * Self-contained thumbnail generation system for meshes.
 * Has its own pipeline, shaders, and descriptor sets - completely independent
 * of the main rendering system.
 */
class ThumbnailGenerator {
public:
    ThumbnailGenerator(VulkanContext* context);
    ~ThumbnailGenerator();
    
    // Initialize the thumbnail generation system
    bool initialize();
    
    // Generate a thumbnail for a mesh and return an ImGui-compatible texture handle
    // Returns 0 on failure
    VkDescriptorSet generateMeshThumbnail(Mesh* mesh, uint32_t size = 128);
    
    // Generate a solid color thumbnail for a material
    VkDescriptorSet generateMaterialThumbnail(const glm::vec3& color, uint32_t size = 128);
    
    // Generate a thumbnail from raw texture data (RGBA format)
    VkDescriptorSet generateTextureThumbnail(const unsigned char* data, int width, int height, uint32_t size = 128);
    
    // Create a thumbnail from pre-existing pixel data (no resizing, expects exact size)
    VkDescriptorSet createThumbnailFromPixels(const unsigned char* pixels, uint32_t width, uint32_t height);
    
    // Release a previously generated thumbnail
    void releaseThumbnail(VkDescriptorSet thumbnail);
    
    // Release all thumbnails
    void releaseAllThumbnails();
    
    // Cleanup all resources
    void cleanup();

private:
    // Vulkan resources
    VulkanContext* m_vulkanContext;
    
    // Pipeline resources (self-contained)
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    
    // Shader modules
    VkShaderModule m_vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule m_fragShaderModule = VK_NULL_HANDLE;
    
    // Offscreen render target (reused for each thumbnail generation)
    VkImage m_renderImage = VK_NULL_HANDLE;
    VkDeviceMemory m_renderImageMemory = VK_NULL_HANDLE;
    VkImageView m_renderImageView = VK_NULL_HANDLE;
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    
    // Current render target size
    uint32_t m_currentSize = 0;
    
    // Uniform buffer for MVP matrix
    VkBuffer m_uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_uniformBufferMemory = VK_NULL_HANDLE;
    VkDescriptorSet m_uniformDescriptorSet = VK_NULL_HANDLE;
    
    // Track generated thumbnails for cleanup
    struct ThumbnailResource {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView imageView;
        VkDescriptorSet descriptorSet;
    };
    std::vector<ThumbnailResource> m_generatedThumbnails;
    
    // Push constant structure
    struct PushConstants {
        glm::mat4 model;
        glm::vec4 color;
    };
    
    // Uniform buffer structure
    struct UniformBufferObject {
        glm::mat4 view;
        glm::mat4 proj;
    };
    
    // Helper functions
    bool createRenderPass();
    bool createPipeline();
    bool createDescriptorResources();
    bool createRenderTarget(uint32_t size);
    void cleanupRenderTarget();
    bool createUniformBuffer();
    
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readShaderFile(const std::string& filename);
    
    // Calculate isometric camera matrices for a mesh
    void calculateIsometricCamera(Mesh* mesh, glm::mat4& view, glm::mat4& proj, float padding = 0.05f);
    
    // Copy render result to a new image for ImGui
    ThumbnailResource copyToThumbnailImage(uint32_t size);
    
    // Auto-fit: find content bounds and blit scaled to fill thumbnail
    ThumbnailResource copyToThumbnailImageAutoFit(uint32_t renderSize, uint32_t outputSize, float padding = 0.08f);
    
    // Find bounding box of non-background pixels in rendered image
    bool findContentBounds(uint32_t size, uint32_t& minX, uint32_t& minY, uint32_t& maxX, uint32_t& maxY);
};

} // namespace Forge
