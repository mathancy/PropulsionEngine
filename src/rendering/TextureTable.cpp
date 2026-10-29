#include "rendering/TextureTable.h"
#include "rendering/VulkanContext.h"
#include "core/CrashHandler.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Propulsion {

namespace {

// How many textures the table holds: plenty, but never more than the GPU can index (the scene shader has a few other samplers too).
uint32_t chooseCapacity(VulkanContext* context) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(context->getPhysicalDevice(), &properties);
    const uint32_t limit = (std::min)(properties.limits.maxPerStageDescriptorSamplers, properties.limits.maxPerStageDescriptorSampledImages);
    const uint32_t room = limit > 64 ? limit - 32 : 32;
    return (std::min)(1024u, room);
}

} // namespace

TextureTable::TextureTable(VulkanContext* context) : m_context(context), m_slots(chooseCapacity(context)) {
    VkDevice device = context->getDevice();
    const uint32_t capacity = m_slots.capacity();

    VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;  // slots not written yet are never read
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount = 1;
    flagsInfo.pBindingFlags = &bindingFlags;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = capacity;
    binding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create the texture table's descriptor set layout!");
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, capacity * kFrames};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kFrames;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create the texture table's descriptor pool!");
    }

    std::array<VkDescriptorSetLayout, kFrames> layouts;
    layouts.fill(m_setLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = kFrames;
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &allocInfo, m_sets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate the texture table's descriptor sets!");
    }

    createWhiteTexture();
    m_infos.push_back({m_whiteSampler, m_whiteView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});  // slot 0
    CrashHandler::logInfo("Texture table ready: room for " + std::to_string(capacity) + " textures");
}

TextureTable::~TextureTable() {
    VkDevice device = m_context->getDevice();
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    vkDestroySampler(device, m_whiteSampler, nullptr);
    vkDestroyImageView(device, m_whiteView, nullptr);
    vkDestroyImage(device, m_whiteImage, nullptr);
    vkFreeMemory(device, m_whiteMemory, nullptr);
}

int32_t TextureTable::add(VkImageView view, VkSampler sampler) {
    if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) return kNone;
    const bool isNew = !m_slots.has(reinterpret_cast<uint64_t>(view), reinterpret_cast<uint64_t>(sampler));
    const int32_t slot = m_slots.slotFor(reinterpret_cast<uint64_t>(view), reinterpret_cast<uint64_t>(sampler));
    if (slot == kNone) {
        CrashHandler::logWarning("The texture table is full; a texture will show as white");
        return kNone;
    }
    if (isNew) m_infos.push_back({sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    return slot;
}

void TextureTable::flush(uint32_t frame) {
    const uint32_t index = frame % kFrames;
    const uint32_t have = m_written[index];
    const uint32_t want = static_cast<uint32_t>(m_infos.size());
    if (have >= want) return;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_sets[index];
    write.dstBinding = 0;
    write.dstArrayElement = have;
    write.descriptorCount = want - have;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = m_infos.data() + have;
    vkUpdateDescriptorSets(m_context->getDevice(), 1, &write, 0, nullptr);
    m_written[index] = want;
}

void TextureTable::createWhiteTexture() {
    VkDevice device = m_context->getDevice();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {1, 1, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &m_whiteImage) != VK_SUCCESS) throw std::runtime_error("Failed to create the white texture!");

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, m_whiteImage, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = m_context->findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &alloc, nullptr, &m_whiteMemory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate the white texture!");
    vkBindImageMemory(device, m_whiteImage, m_whiteMemory, 0);

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    m_context->createBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            staging, stagingMemory);
    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, 4, 0, &mapped);
    std::memset(mapped, 0xFF, 4);
    vkUnmapMemory(device, stagingMemory);

    VkCommandBuffer commandBuffer = m_context->beginSingleTimeCommands();
    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = m_whiteImage;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(commandBuffer, staging, m_whiteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toRead = toTransfer;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
    m_context->endSingleTimeCommands(commandBuffer);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_whiteImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_whiteView) != VK_SUCCESS) throw std::runtime_error("Failed to create the white texture's view!");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_whiteSampler) != VK_SUCCESS) throw std::runtime_error("Failed to create the white texture's sampler!");
}

} // namespace Propulsion
