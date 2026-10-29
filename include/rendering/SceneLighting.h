#pragma once

#include "rendering/EnvironmentMap.h"
#include "rendering/LightingData.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Propulsion {

class VulkanContext;

/**
 * SceneLighting
 * What every lit shader shares: the lights and the environment (the sky or HDRI that lights the scene and shows up in
 * reflections). It owns one descriptor set per frame in flight and per slot, holding the block of lights (a uniform
 * buffer) and the environment image (a half-float panorama with a chain of blurrier mips for reflections).
 *
 * A shader that wants it lists `getSetLayout()` as one of its descriptor set layouts (Shader takes it as a
 * constructor argument), and the draw code calls `bind` once after binding the pipeline.
 *
 * Slots exist because the level viewport and the asset preview are both drawn in one frame and need different lights.
 * Everything here is written before the frame's command buffer starts recording (`update`, `setEnvironment`).
 */
class SceneLighting {
public:
    enum class Slot : uint32_t { Scene = 0, Preview = 1 };
    static constexpr uint32_t kSlotCount = 2;

    /// `skyRenderPass` is the render pass the sky is drawn in (the offscreen scene pass; the asset preview shares it).
    SceneLighting(VulkanContext* context, VkRenderPass skyRenderPass);
    ~SceneLighting();

    SceneLighting(const SceneLighting&) = delete;
    SceneLighting& operator=(const SceneLighting&) = delete;

    VkDescriptorSetLayout getSetLayout() const { return m_setLayout; }

    /// Use a new sky. Waits for the GPU to finish with the old one, so call it between frames (not while a command
    /// buffer is being recorded).
    void setEnvironment(const Environment::Prepared& prepared);

    /// The shadow maps the scene shader reads (made by ShadowRenderer). Set once, before the first frame.
    void setShadowMaps(VkImageView directional, VkImageView pointArray, VkSampler sampler);

    /// The ambient occlusion the scene (not the previews) is shaded with in this frame; a null view means none (white).
    /// Written before the frame is recorded, like `update`.
    void setSceneAmbientOcclusion(uint32_t frame, VkImageView view, VkSampler sampler);

    /// Hybrid RT: the acceleration structure the scene shader traces shadows and occlusion through in this frame (binding 5). Only
    /// when the GPU can do ray queries; written before the frame is recorded. The set always has the binding on such a GPU, but a
    /// shader that doesn't trace (the normal one) never reads it.
    void setSceneAccelerationStructure(uint32_t frame, VkAccelerationStructureKHR structure);

    /// The highest mip of the reflection image: a shader reflects at (roughness * maxLod).
    float maxLod() const { return m_maxLod; }

    /// Write the lights and environment parameters for a frame. Before that frame's command buffer is recorded.
    void update(uint32_t frame, Slot slot, const GpuLighting& data);

    /// Bind the lighting set at `setIndex` of a pipeline layout that includes `getSetLayout()`.
    void bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex, uint32_t frame, Slot slot) const;

    /// Draw the sky behind everything as a full-screen triangle, inside the render pass given to the constructor.
    void drawSky(VkCommandBuffer commandBuffer, uint32_t frame, Slot slot, const glm::mat4& view, const glm::mat4& projection,
                 float exposure) const;

private:
    void createSetLayout();
    void createBuffersAndSets();
    void createSampler();
    void createWhiteTexture();
    void writeSampledImage(VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout layout);
    void createSkyPipeline(VkRenderPass renderPass);
    void destroyEnvironmentImage();
    void writeEnvironmentDescriptors();
    uint32_t setIndexOf(uint32_t frame, Slot slot) const;

    VulkanContext* m_context;

    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_sets;       // [frame * kSlotCount + slot]
    std::vector<VkBuffer> m_buffers;           // one lights block per set
    std::vector<VkDeviceMemory> m_bufferMemory;
    std::vector<void*> m_mapped;

    VkSampler m_sampler = VK_NULL_HANDLE;
    VkImage m_whiteImage = VK_NULL_HANDLE;  // one white pixel: "no occlusion"
    VkDeviceMemory m_whiteMemory = VK_NULL_HANDLE;
    VkImageView m_whiteView = VK_NULL_HANDLE;
    VkSampler m_whiteSampler = VK_NULL_HANDLE;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    float m_maxLod = 0.0f;

    VkPipelineLayout m_skyPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_skyPipeline = VK_NULL_HANDLE;
};

} // namespace Propulsion
