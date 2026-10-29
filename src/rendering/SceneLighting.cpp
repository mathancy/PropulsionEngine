#include "rendering/SceneLighting.h"
#include "rendering/VulkanContext.h"
#include "core/CrashHandler.h"

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Propulsion {

namespace {

constexpr int kDefaultSkyWidth = 256;
constexpr uint32_t kFrames = static_cast<uint32_t>(VulkanContext::MAX_FRAMES_IN_FLIGHT);

// Matches `Push` in shaders/sky.frag.
struct SkyPush {
    glm::mat4 invViewProjection;
    glm::vec4 params;  // x = exposure
};

std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open shader file: " + path);
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(size));
    return code;
}

VkShaderModule makeModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }
    return module;
}

void createBuffer(VulkanContext* context, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  VkBuffer& buffer, VkDeviceMemory& memory) {
    VkDevice device = context->getDevice();
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS) throw std::runtime_error("Failed to create buffer!");

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = context->findMemoryType(requirements.memoryTypeBits, properties);
    if (vkAllocateMemory(device, &alloc, nullptr, &memory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate buffer memory!");
    vkBindBufferMemory(device, buffer, memory, 0);
}

} // namespace

SceneLighting::SceneLighting(VulkanContext* context, VkRenderPass skyRenderPass) : m_context(context) {
    createSetLayout();
    createSampler();
    createWhiteTexture();
    createBuffersAndSets();
    createSkyPipeline(skyRenderPass);
    // Until a scene asks for something else, the sky is the default gradient.
    setEnvironment(Environment::prepare(Environment::makeGradientSky(kDefaultSkyWidth)));
    CrashHandler::logInfo("Scene lighting created");
}

SceneLighting::~SceneLighting() {
    VkDevice device = m_context->getDevice();
    vkDeviceWaitIdle(device);

    destroyEnvironmentImage();
    if (m_whiteView != VK_NULL_HANDLE) vkDestroyImageView(device, m_whiteView, nullptr);
    if (m_whiteImage != VK_NULL_HANDLE) vkDestroyImage(device, m_whiteImage, nullptr);
    if (m_whiteMemory != VK_NULL_HANDLE) vkFreeMemory(device, m_whiteMemory, nullptr);
    if (m_whiteSampler != VK_NULL_HANDLE) vkDestroySampler(device, m_whiteSampler, nullptr);
    if (m_skyPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, m_skyPipeline, nullptr);
    if (m_skyPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_skyPipelineLayout, nullptr);
    for (size_t i = 0; i < m_buffers.size(); i++) {
        vkDestroyBuffer(device, m_buffers[i], nullptr);
        vkFreeMemory(device, m_bufferMemory[i], nullptr);
    }
    if (m_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_pool, nullptr);  // frees the sets with it
    if (m_sampler != VK_NULL_HANDLE) vkDestroySampler(device, m_sampler, nullptr);
    if (m_setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
}

uint32_t SceneLighting::setIndexOf(uint32_t frame, Slot slot) const {
    return (frame % kFrames) * kSlotCount + static_cast<uint32_t>(slot);
}

void SceneLighting::createSetLayout() {
    // On a GPU with ray queries there is a sixth binding: the acceleration structure, for the Hybrid RT version of the scene shader.
    const bool traces = m_context->getRayTracingCaps().canQuery();
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    bindings[0].binding = 0;  // the block of lights and environment parameters
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;  // the environment panorama
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    for (uint32_t i = 2; i < 5; i++) {  // 2: the sun's shadow map, 3: the point lights' shadow maps (an array), 4: ambient occlusion
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    bindings[5].binding = 5;  // the scene's acceleration structure
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = traces ? 6u : 5u;
    info.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(m_context->getDevice(), &info, nullptr, &m_setLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting descriptor set layout!");
    }
}

void SceneLighting::setSceneAccelerationStructure(uint32_t frame, VkAccelerationStructureKHR structure) {
    if (!m_context->getRayTracingCaps().canQuery() || structure == VK_NULL_HANDLE) return;

    VkWriteDescriptorSetAccelerationStructureKHR structureWrite{};
    structureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    structureWrite.accelerationStructureCount = 1;
    structureWrite.pAccelerationStructures = &structure;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = &structureWrite;
    write.dstSet = m_sets[setIndexOf(frame, Slot::Scene)];
    write.dstBinding = 5;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    write.descriptorCount = 1;
    vkUpdateDescriptorSets(m_context->getDevice(), 1, &write, 0, nullptr);
}

void SceneLighting::createSampler() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;           // around the panorama
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;    // not over the poles
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.minLod = 0.0f;
    info.maxLod = VK_LOD_CLAMP_NONE;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (vkCreateSampler(m_context->getDevice(), &info, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create environment sampler!");
    }
}

void SceneLighting::createBuffersAndSets() {
    VkDevice device = m_context->getDevice();
    const uint32_t setCount = kFrames * kSlotCount;

    std::array<VkDescriptorPoolSize, 3> sizes{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount};
    sizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * setCount};
    sizes[2] = {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, setCount};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = m_context->getRayTracingCaps().canQuery() ? 3u : 2u;
    poolInfo.pPoolSizes = sizes.data();
    poolInfo.maxSets = setCount;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lighting descriptor pool!");
    }

    std::vector<VkDescriptorSetLayout> layouts(setCount, m_setLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = setCount;
    allocInfo.pSetLayouts = layouts.data();
    m_sets.resize(setCount);
    if (vkAllocateDescriptorSets(device, &allocInfo, m_sets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate lighting descriptor sets!");
    }

    m_buffers.resize(setCount);
    m_bufferMemory.resize(setCount);
    m_mapped.resize(setCount);
    for (uint32_t i = 0; i < setCount; i++) {
        createBuffer(m_context, sizeof(GpuLighting), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_buffers[i], m_bufferMemory[i]);
        vkMapMemory(device, m_bufferMemory[i], 0, sizeof(GpuLighting), 0, &m_mapped[i]);
        std::memset(m_mapped[i], 0, sizeof(GpuLighting));

        VkDescriptorBufferInfo bufferInfo{m_buffers[i], 0, sizeof(GpuLighting)};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_sets[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        // No occlusion until the scene says otherwise (the previews never have any).
        writeSampledImage(m_sets[i], 4, m_whiteView, m_whiteSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void SceneLighting::writeSampledImage(VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout layout) {
    VkDescriptorImageInfo imageInfo{sampler, view, layout};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_context->getDevice(), 1, &write, 0, nullptr);
}

void SceneLighting::createWhiteTexture() {
    VkDevice device = m_context->getDevice();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8_UNORM;
    imageInfo.extent = {1, 1, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &m_whiteImage) != VK_SUCCESS) throw std::runtime_error("Failed to create white texture!");
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, m_whiteImage, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = m_context->findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &alloc, nullptr, &m_whiteMemory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate white texture memory!");
    vkBindImageMemory(device, m_whiteImage, m_whiteMemory, 0);

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    createBuffer(m_context, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMemory);
    void* data = nullptr;
    vkMapMemory(device, stagingMemory, 0, 4, 0, &data);
    std::memset(data, 255, 4);
    vkUnmapMemory(device, stagingMemory);

    VkCommandBuffer commandBuffer = m_context->beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_whiteImage;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(commandBuffer, staging, m_whiteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    m_context->endSingleTimeCommands(commandBuffer);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_whiteImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_whiteView) != VK_SUCCESS) throw std::runtime_error("Failed to create white texture view!");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_whiteSampler) != VK_SUCCESS) throw std::runtime_error("Failed to create sampler!");
}

void SceneLighting::setShadowMaps(VkImageView directional, VkImageView pointArray, VkSampler sampler) {
    for (VkDescriptorSet set : m_sets) {
        writeSampledImage(set, 2, directional, sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        writeSampledImage(set, 3, pointArray, sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }
}

void SceneLighting::setSceneAmbientOcclusion(uint32_t frame, VkImageView view, VkSampler sampler) {
    const VkDescriptorSet set = m_sets[setIndexOf(frame, Slot::Scene)];
    if (view == VK_NULL_HANDLE) writeSampledImage(set, 4, m_whiteView, m_whiteSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    else writeSampledImage(set, 4, view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void SceneLighting::createSkyPipeline(VkRenderPass renderPass) {
    VkDevice device = m_context->getDevice();

    VkShaderModule vert = makeModule(device, readFile("shaders/sky_vert.spv"));
    VkShaderModule frag = makeModule(device, readFile("shaders/sky_frag.spv"));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};  // no vertex buffer: the triangle comes from gl_VertexIndex
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // The sky is drawn first and everything else covers it, so it neither tests nor writes depth.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyPush)};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_skyPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create sky pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_skyPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_skyPipeline);

    vkDestroyShaderModule(device, frag, nullptr);
    vkDestroyShaderModule(device, vert, nullptr);
    if (result != VK_SUCCESS) throw std::runtime_error("Failed to create sky pipeline!");
}

void SceneLighting::destroyEnvironmentImage() {
    VkDevice device = m_context->getDevice();
    if (m_imageView != VK_NULL_HANDLE) vkDestroyImageView(device, m_imageView, nullptr);
    if (m_image != VK_NULL_HANDLE) vkDestroyImage(device, m_image, nullptr);
    if (m_imageMemory != VK_NULL_HANDLE) vkFreeMemory(device, m_imageMemory, nullptr);
    m_imageView = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE;
    m_imageMemory = VK_NULL_HANDLE;
}

void SceneLighting::writeEnvironmentDescriptors() {
    VkDescriptorImageInfo imageInfo{m_sampler, m_imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (VkDescriptorSet set : m_sets) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_context->getDevice(), 1, &write, 0, nullptr);
    }
}

void SceneLighting::setEnvironment(const Environment::Prepared& prepared) {
    const std::vector<Environment::Image>& levels = prepared.chain.levels;
    if (levels.empty()) return;

    VkDevice device = m_context->getDevice();
    vkDeviceWaitIdle(device);  // nothing may still be reading the image we are about to replace
    destroyEnvironmentImage();

    // Every mip level, one after another, as half floats.
    std::vector<uint16_t> texels;
    std::vector<VkBufferImageCopy> regions;
    for (size_t i = 0; i < levels.size(); i++) {
        const std::vector<uint16_t> level = Environment::toRgba16f(levels[i]);
        VkBufferImageCopy region{};
        region.bufferOffset = texels.size() * sizeof(uint16_t);
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i), 0, 1};
        region.imageExtent = {static_cast<uint32_t>(levels[i].width), static_cast<uint32_t>(levels[i].height), 1};
        regions.push_back(region);
        texels.insert(texels.end(), level.begin(), level.end());
    }
    const VkDeviceSize byteSize = texels.size() * sizeof(uint16_t);
    const uint32_t mipLevels = static_cast<uint32_t>(levels.size());

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    createBuffer(m_context, byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMemory);
    void* data = nullptr;
    vkMapMemory(device, stagingMemory, 0, byteSize, 0, &data);
    std::memcpy(data, texels.data(), static_cast<size_t>(byteSize));
    vkUnmapMemory(device, stagingMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {static_cast<uint32_t>(levels[0].width), static_cast<uint32_t>(levels[0].height), 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) throw std::runtime_error("Failed to create environment image!");

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, m_image, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = m_context->findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &alloc, nullptr, &m_imageMemory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate environment image memory!");
    vkBindImageMemory(device, m_image, m_imageMemory, 0);

    VkCommandBuffer commandBuffer = m_context->beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};

    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkCmdCopyBufferToImage(commandBuffer, staging, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    m_context->endSingleTimeCommands(commandBuffer);

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) throw std::runtime_error("Failed to create environment image view!");

    m_maxLod = static_cast<float>(mipLevels - 1);
    writeEnvironmentDescriptors();
    CrashHandler::logInfo("Environment uploaded: " + std::to_string(levels[0].width) + "x" + std::to_string(levels[0].height) + ", " +
                          std::to_string(mipLevels) + " levels, " + std::to_string(byteSize / 1024) + " KiB");
}

void SceneLighting::update(uint32_t frame, Slot slot, const GpuLighting& data) {
    std::memcpy(m_mapped[setIndexOf(frame, slot)], &data, sizeof(GpuLighting));
}

void SceneLighting::bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t setIndex, uint32_t frame, Slot slot) const {
    const VkDescriptorSet set = m_sets[setIndexOf(frame, slot)];
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, setIndex, 1, &set, 0, nullptr);
}

void SceneLighting::drawSky(VkCommandBuffer commandBuffer, uint32_t frame, Slot slot, const glm::mat4& view, const glm::mat4& projection,
                            float exposure) const {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline);
    const VkDescriptorSet set = m_sets[setIndexOf(frame, slot)];
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipelineLayout, 0, 1, &set, 0, nullptr);

    SkyPush push{};
    push.invViewProjection = glm::inverse(projection * view);
    push.params = glm::vec4(exposure, 0.0f, 0.0f, 0.0f);
    vkCmdPushConstants(commandBuffer, m_skyPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyPush), &push);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

} // namespace Propulsion
