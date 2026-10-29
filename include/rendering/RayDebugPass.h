#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>

namespace Propulsion {

class VulkanContext;
class RayTracing;

/**
 * RayDebugPass
 * The check that the ray tracing foundation works: a compute pass that shoots one primary ray per pixel from the camera
 * through the scene's acceleration structure (a ray query) and draws what it hits, over the viewport image. Not a render
 * mode: it is a developer view, switched on from the Environment section of the Objects panel.
 */
class RayDebugPass {
public:
    enum class Mode : uint32_t {
        Normals = 0,    // the smooth world-space normal as a colour
        Instances = 1,  // a flat colour per object
    };

    explicit RayDebugPass(VulkanContext* context);
    ~RayDebugPass();

    RayDebugPass(const RayDebugPass&) = delete;
    RayDebugPass& operator=(const RayDebugPass&) = delete;

    /// Records the pass. `image` is the viewport image: it must be in SHADER_READ_ONLY_OPTIMAL (where the scene pass leaves
    /// it), is overwritten, and is left there. Call after RayTracing::update for this frame, outside any render pass.
    void render(VkCommandBuffer commandBuffer, uint32_t frame, const RayTracing& scene, VkImage image, VkImageView imageView,
                VkExtent2D extent, const glm::mat4& viewProjection, const glm::vec3& cameraPosition, Mode mode);

private:
    VulkanContext* m_context;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> m_sets{};
};

} // namespace Propulsion
