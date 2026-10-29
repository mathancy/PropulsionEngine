#pragma once

#include "rendering/LightingData.h"
#include "rendering/Mesh.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace Propulsion {

class VulkanContext;

/**
 * ShadowRenderer
 * Draws the scene's depth from the lights' points of view: a few maps for the sun (cascades: the nearest is sharpest and
 * covers least of the view), and six faces for each of the first few point lights (the matrices come from ShadowFrame, see rendering/ShadowMath.h and LightingData.h). The maps are read by
 * the scene shader through SceneLighting, which does the soft filtering (shaders/lighting.glsl).
 *
 * `render` goes into the frame's command buffer before the scene pass. The maps are sampled afterwards in the same
 * frame, and the render pass's dependencies keep the next frame from overwriting them while they are still being read.
 */
class ShadowRenderer {
public:
    /// Something that casts a shadow: a mesh in the world, and a sphere around it (to skip it where a light can't reach).
    struct Caster {
        Mesh* mesh = nullptr;
        glm::mat4 model{1.0f};
        glm::vec3 center{0.0f};
        float radius = 0.0f;
    };

    explicit ShadowRenderer(VulkanContext* context);
    ~ShadowRenderer();

    ShadowRenderer(const ShadowRenderer&) = delete;
    ShadowRenderer& operator=(const ShadowRenderer&) = delete;

    /// The sun's maps as an array (one layer per cascade).
    VkImageView getDirectionalView() const { return m_directionalArrayView; }
    VkImageView getPointArrayView() const { return m_pointArrayView; }
    VkSampler getSampler() const { return m_sampler; }

    void render(VkCommandBuffer commandBuffer, const ShadowFrame& frame, const std::vector<Caster>& casters);

private:
    struct Target {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    void createTarget(Target& target, uint32_t size, uint32_t layers);
    void createRenderPass();
    void createPipeline();
    void clearAll();
    void drawCasters(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, uint32_t size, const glm::mat4& viewProjection,
                     const std::vector<Caster>& casters, const glm::vec3* lightPosition, float lightReach,
                     const Shadows::DirectionalShadow* map);

    VulkanContext* m_context;
    VkFormat m_format = VK_FORMAT_D32_SFLOAT;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    Target m_directional;                              // Shadows::kCascadeCount layers
    VkImageView m_directionalArrayView = VK_NULL_HANDLE;
    std::vector<VkImageView> m_directionalLayerViews;
    std::vector<VkFramebuffer> m_directionalFramebuffers;

    Target m_points;                                   // Shadows::kMaxPointShadows * 6 layers
    VkImageView m_pointArrayView = VK_NULL_HANDLE;
    std::vector<VkImageView> m_pointLayerViews;        // one per layer, to draw into
    std::vector<VkFramebuffer> m_pointFramebuffers;
};

} // namespace Propulsion
