#pragma once

#include "rendering/Mesh.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace Propulsion {

class VulkanContext;
class Shader;

/**
 * AmbientOcclusion
 * Screen-space ambient occlusion for the scene pass. Three passes before it:
 *   1. a pre-pass drawing the scene's depth and view-space normals (the same objects, through the scene shader's own
 *      per-object uniforms, with shaders/prepass.frag);
 *   2. the occlusion itself (shaders/ssao.frag), noisy;
 *   3. a depth-aware blur (shaders/ssao_blur.frag).
 * `getResultView` is then read by the scene shader (through SceneLighting) to dim the sky's light where it is blocked.
 * Direct lights are not touched by it; they have shadows.
 */
class AmbientOcclusion {
public:
    struct Draw {
        Mesh* mesh = nullptr;
        uint32_t block = 0;  // which per-object block of the scene shader's uniform buffer this mesh uses
    };

    struct Settings {
        float radius = 0.5f;     // how far from a surface things can shade it (world units)
        float strength = 1.0f;   // 0 is none, 1 is full
    };

    /// `sceneShader` supplies the descriptor set layout of the per-object uniforms (set 0) and the offscreen render
    /// target's size is used for everything here.
    AmbientOcclusion(VulkanContext* context, Shader* sceneShader);
    ~AmbientOcclusion();

    AmbientOcclusion(const AmbientOcclusion&) = delete;
    AmbientOcclusion& operator=(const AmbientOcclusion&) = delete;

    VkImageView getResultView() const { return m_blurred.view; }
    VkSampler getSampler() const { return m_linearSampler; }

    /// Record the three passes into the frame's command buffer, before the scene pass.
    void render(VkCommandBuffer commandBuffer, uint32_t frame, Shader& sceneShader, const std::vector<Draw>& draws, const glm::mat4& projection,
                const Settings& settings);

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    void createImage(Image& out, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
    void createRenderPasses();
    void createFramebuffers();
    void createPipelines(VkDescriptorSetLayout sceneSetLayout);
    void createDescriptors();
    void destroyImage(Image& image);

    VulkanContext* m_context;
    VkExtent2D m_extent{};
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;

    Image m_normals;   // view-space normals, packed in two numbers
    Image m_depth;
    Image m_raw;       // occlusion before the blur
    Image m_blurred;   // occlusion after it: what the scene shader reads

    VkRenderPass m_prepassRenderPass = VK_NULL_HANDLE;
    VkRenderPass m_aoRenderPass = VK_NULL_HANDLE;  // for both the occlusion and the blur: one 8-bit target
    VkFramebuffer m_prepassFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer m_rawFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer m_blurredFramebuffer = VK_NULL_HANDLE;

    VkPipelineLayout m_prepassLayout = VK_NULL_HANDLE;
    VkPipeline m_prepassPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_aoSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_blurSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_aoLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_blurLayout = VK_NULL_HANDLE;
    VkPipeline m_aoPipeline = VK_NULL_HANDLE;
    VkPipeline m_blurPipeline = VK_NULL_HANDLE;

    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_aoSets;     // per frame in flight
    std::vector<VkDescriptorSet> m_blurSets;
    std::vector<VkBuffer> m_uniformBuffers;    // the projection, the settings and the sample pattern, per frame
    std::vector<VkDeviceMemory> m_uniformMemory;
    std::vector<void*> m_uniformMapped;

    VkSampler m_nearestSampler = VK_NULL_HANDLE;
    VkSampler m_linearSampler = VK_NULL_HANDLE;
    std::vector<glm::vec4> m_kernel;
};

} // namespace Propulsion
