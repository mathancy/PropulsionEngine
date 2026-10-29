#pragma once

#include "rendering/TextureSlots.h"

#include <vulkan/vulkan.h>
#include <array>
#include <vector>

namespace Propulsion {

class VulkanContext;

/**
 * TextureTable
 * Every texture a material uses, in one array a shader can index (a "bindless" table): the scene shader (and later the ray tracing
 * shaders) pick a texture by the slot number that is in the object's uniform block. That is what lets every object have its own
 * textures in one frame; before, only the first textured object's textures could be bound.
 *
 * `add` gives a texture its slot, once; slots never change or get reused while the editor runs. There is one descriptor set for each
 * frame in flight: `flush` brings the frame's set up to date with the textures added since, so call it at the start of a frame,
 * before recording (the frame's fence has been waited for, so nothing is using that set). Slot 0 is a white pixel, for anything
 * that points at nothing.
 *
 * Shader side: `layout(set = N, binding = 0) uniform sampler2D textures[];` indexed with nonuniformEXT(slot).
 */
class TextureTable {
public:
    static constexpr uint32_t kFrames = 2;
    static constexpr int32_t kNone = TextureSlots::kNone;

    explicit TextureTable(VulkanContext* context);
    ~TextureTable();

    TextureTable(const TextureTable&) = delete;
    TextureTable& operator=(const TextureTable&) = delete;

    VkDescriptorSetLayout getSetLayout() const { return m_setLayout; }
    VkDescriptorSet getSet(uint32_t frame) const { return m_sets[frame % kFrames]; }
    uint32_t capacity() const { return m_slots.capacity(); }
    uint32_t used() const { return m_slots.used(); }

    /// The slot for a texture (an image view in SHADER_READ_ONLY_OPTIMAL and the sampler to read it with). kNone if the table is full.
    int32_t add(VkImageView view, VkSampler sampler);

    /// Writes the textures added since this frame's set was last brought up to date.
    void flush(uint32_t frame);

private:
    void createWhiteTexture();

    VulkanContext* m_context;
    TextureSlots m_slots;
    std::vector<VkDescriptorImageInfo> m_infos;  // [slot]
    std::array<uint32_t, kFrames> m_written{};   // how many slots each frame's set has

    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFrames> m_sets{};

    VkImage m_whiteImage = VK_NULL_HANDLE;
    VkDeviceMemory m_whiteMemory = VK_NULL_HANDLE;
    VkImageView m_whiteView = VK_NULL_HANDLE;
    VkSampler m_whiteSampler = VK_NULL_HANDLE;
};

} // namespace Propulsion
