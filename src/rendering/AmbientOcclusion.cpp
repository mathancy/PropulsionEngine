#include "rendering/AmbientOcclusion.h"
#include "rendering/Shader.h"
#include "rendering/VulkanContext.h"
#include "core/CrashHandler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

namespace Propulsion {

namespace {

constexpr int kSampleCount = 16;
constexpr uint32_t kFrames = static_cast<uint32_t>(VulkanContext::MAX_FRAMES_IN_FLIGHT);

// Matches `AoParams` in shaders/ssao.glsl.
struct AoUniforms {
    glm::mat4 projection;
    glm::mat4 inverseProjection;
    glm::vec4 params;  // x = radius, y = bias, z = strength, w = power
    glm::vec4 kernel[kSampleCount];
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

VkShaderModule makeModule(VkDevice device, const std::string& path) {
    const std::vector<char> code = readFile(path);
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) throw std::runtime_error("Failed to create shader module: " + path);
    return module;
}

} // namespace

AmbientOcclusion::AmbientOcclusion(VulkanContext* context, Shader* sceneShader) : m_context(context) {
    m_extent = m_context->getOffscreenExtent();
    m_depthFormat = m_context->findSupportedFormat({VK_FORMAT_D32_SFLOAT}, VK_IMAGE_TILING_OPTIMAL,
                                                   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    // A fixed pattern of sample offsets in the hemisphere above a surface, more of them close in.
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int i = 0; i < kSampleCount; i++) {
        glm::vec3 s(unit(rng) * 2.0f - 1.0f, unit(rng) * 2.0f - 1.0f, unit(rng));
        s = glm::normalize(s) * unit(rng);
        float scale = static_cast<float>(i) / static_cast<float>(kSampleCount);
        scale = 0.25f + 0.75f * scale;  // spread out to the full radius, a few close in
        m_kernel.push_back(glm::vec4(s * scale, 0.0f));
    }

    createImage(m_normals, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    createImage(m_depth, m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    createImage(m_raw, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    createImage(m_blurred, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    createRenderPasses();
    createFramebuffers();
    createDescriptors();
    createPipelines(sceneShader->getDescriptorSetLayout());
    CrashHandler::logInfo("Ambient occlusion created at " + std::to_string(m_extent.width) + "x" + std::to_string(m_extent.height));
}

AmbientOcclusion::~AmbientOcclusion() {
    VkDevice device = m_context->getDevice();
    vkDeviceWaitIdle(device);
    for (VkPipeline p : {m_prepassPipeline, m_aoPipeline, m_blurPipeline}) if (p != VK_NULL_HANDLE) vkDestroyPipeline(device, p, nullptr);
    for (VkPipelineLayout l : {m_prepassLayout, m_aoLayout, m_blurLayout}) if (l != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, l, nullptr);
    if (m_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_pool, nullptr);
    for (VkDescriptorSetLayout l : {m_aoSetLayout, m_blurSetLayout}) if (l != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, l, nullptr);
    for (size_t i = 0; i < m_uniformBuffers.size(); i++) {
        vkDestroyBuffer(device, m_uniformBuffers[i], nullptr);
        vkFreeMemory(device, m_uniformMemory[i], nullptr);
    }
    for (VkFramebuffer f : {m_prepassFramebuffer, m_rawFramebuffer, m_blurredFramebuffer}) if (f != VK_NULL_HANDLE) vkDestroyFramebuffer(device, f, nullptr);
    for (VkRenderPass r : {m_prepassRenderPass, m_aoRenderPass}) if (r != VK_NULL_HANDLE) vkDestroyRenderPass(device, r, nullptr);
    for (VkSampler s : {m_nearestSampler, m_linearSampler}) if (s != VK_NULL_HANDLE) vkDestroySampler(device, s, nullptr);
    for (Image* image : {&m_normals, &m_depth, &m_raw, &m_blurred}) destroyImage(*image);
}

void AmbientOcclusion::destroyImage(Image& image) {
    VkDevice device = m_context->getDevice();
    if (image.view != VK_NULL_HANDLE) vkDestroyImageView(device, image.view, nullptr);
    if (image.image != VK_NULL_HANDLE) vkDestroyImage(device, image.image, nullptr);
    if (image.memory != VK_NULL_HANDLE) vkFreeMemory(device, image.memory, nullptr);
    image = Image();
}

void AmbientOcclusion::createImage(Image& out, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    VkDevice device = m_context->getDevice();
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {m_extent.width, m_extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &info, nullptr, &out.image) != VK_SUCCESS) throw std::runtime_error("Failed to create ambient occlusion image!");

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, out.image, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = m_context->findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &alloc, nullptr, &out.memory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate ambient occlusion memory!");
    vkBindImageMemory(device, out.image, out.memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = out.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {aspect, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &out.view) != VK_SUCCESS) throw std::runtime_error("Failed to create ambient occlusion view!");
}

void AmbientOcclusion::createRenderPasses() {
    VkDevice device = m_context->getDevice();

    // Pre-pass: normals and depth, both read by the passes after it.
    {
        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = VK_FORMAT_R16G16_SFLOAT;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1] = attachments[0];
        attachments[1].format = m_depthFormat;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;  // the last frame's passes may still be reading these
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = static_cast<uint32_t>(dependencies.size());
        info.pDependencies = dependencies.data();
        if (vkCreateRenderPass(device, &info, nullptr, &m_prepassRenderPass) != VK_SUCCESS) throw std::runtime_error("Failed to create pre-pass render pass!");
    }

    // The occlusion and its blur: one 8-bit target each, read by the next pass.
    {
        VkAttachmentDescription color{};
        color.format = VK_FORMAT_R8_UNORM;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &color;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = static_cast<uint32_t>(dependencies.size());
        info.pDependencies = dependencies.data();
        if (vkCreateRenderPass(device, &info, nullptr, &m_aoRenderPass) != VK_SUCCESS) throw std::runtime_error("Failed to create occlusion render pass!");
    }
}

void AmbientOcclusion::createFramebuffers() {
    VkDevice device = m_context->getDevice();
    auto make = [&](VkRenderPass pass, const std::vector<VkImageView>& views, VkFramebuffer& out) {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = pass;
        info.attachmentCount = static_cast<uint32_t>(views.size());
        info.pAttachments = views.data();
        info.width = m_extent.width;
        info.height = m_extent.height;
        info.layers = 1;
        if (vkCreateFramebuffer(device, &info, nullptr, &out) != VK_SUCCESS) throw std::runtime_error("Failed to create ambient occlusion framebuffer!");
    };
    make(m_prepassRenderPass, {m_normals.view, m_depth.view}, m_prepassFramebuffer);
    make(m_aoRenderPass, {m_raw.view}, m_rawFramebuffer);
    make(m_aoRenderPass, {m_blurred.view}, m_blurredFramebuffer);
}

void AmbientOcclusion::createDescriptors() {
    VkDevice device = m_context->getDevice();

    auto makeSampler = [&](VkFilter filter, VkSampler& out) {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = info.minFilter = filter;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &info, nullptr, &out) != VK_SUCCESS) throw std::runtime_error("Failed to create sampler!");
    };
    makeSampler(VK_FILTER_NEAREST, m_nearestSampler);
    makeSampler(VK_FILTER_LINEAR, m_linearSampler);

    auto makeLayout = [&](VkDescriptorSetLayout& out) {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        for (uint32_t i = 0; i < 2; i++) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &out) != VK_SUCCESS) throw std::runtime_error("Failed to create descriptor set layout!");
    };
    makeLayout(m_aoSetLayout);    // 0 = depth, 1 = normals, 2 = settings
    makeLayout(m_blurSetLayout);  // 0 = noisy occlusion, 1 = depth, 2 = settings

    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * kFrames};
    sizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * kFrames};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    poolInfo.maxSets = 2 * kFrames;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) throw std::runtime_error("Failed to create descriptor pool!");

    m_aoSets.resize(kFrames);
    m_blurSets.resize(kFrames);
    m_uniformBuffers.resize(kFrames);
    m_uniformMemory.resize(kFrames);
    m_uniformMapped.resize(kFrames);
    for (uint32_t f = 0; f < kFrames; f++) {
        for (int which = 0; which < 2; which++) {
            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = m_pool;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = which == 0 ? &m_aoSetLayout : &m_blurSetLayout;
            if (vkAllocateDescriptorSets(device, &alloc, which == 0 ? &m_aoSets[f] : &m_blurSets[f]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate descriptor set!");
            }
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(AoUniforms);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_uniformBuffers[f]) != VK_SUCCESS) throw std::runtime_error("Failed to create buffer!");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, m_uniformBuffers[f], &requirements);
        VkMemoryAllocateInfo memory{};
        memory.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory.allocationSize = requirements.size;
        memory.memoryTypeIndex = m_context->findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &memory, nullptr, &m_uniformMemory[f]) != VK_SUCCESS) throw std::runtime_error("Failed to allocate buffer memory!");
        vkBindBufferMemory(device, m_uniformBuffers[f], m_uniformMemory[f], 0);
        vkMapMemory(device, m_uniformMemory[f], 0, sizeof(AoUniforms), 0, &m_uniformMapped[f]);

        VkDescriptorBufferInfo ubo{m_uniformBuffers[f], 0, sizeof(AoUniforms)};
        VkDescriptorImageInfo depthInfo{m_nearestSampler, m_depth.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo normalInfo{m_nearestSampler, m_normals.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo rawInfo{m_nearestSampler, m_raw.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        std::array<VkWriteDescriptorSet, 6> writes{};
        auto setImage = [&](int i, VkDescriptorSet set, uint32_t binding, const VkDescriptorImageInfo* info) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = binding;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
            writes[i].pImageInfo = info;
        };
        auto setBuffer = [&](int i, VkDescriptorSet set) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = 2;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[i].descriptorCount = 1;
            writes[i].pBufferInfo = &ubo;
        };
        setImage(0, m_aoSets[f], 0, &depthInfo);
        setImage(1, m_aoSets[f], 1, &normalInfo);
        setBuffer(2, m_aoSets[f]);
        setImage(3, m_blurSets[f], 0, &rawInfo);
        setImage(4, m_blurSets[f], 1, &depthInfo);
        setBuffer(5, m_blurSets[f]);
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void AmbientOcclusion::createPipelines(VkDescriptorSetLayout sceneSetLayout) {
    VkDevice device = m_context->getDevice();

    // Everything shares these.
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
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

    auto makeLayout = [&](VkDescriptorSetLayout setLayout, VkPipelineLayout& out) {
        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = 1;
        info.pSetLayouts = &setLayout;
        if (vkCreatePipelineLayout(device, &info, nullptr, &out) != VK_SUCCESS) throw std::runtime_error("Failed to create pipeline layout!");
    };
    // The pre-pass uses the scene shader's own set 0 (so it can use the very same per-object uniform blocks).
    makeLayout(sceneSetLayout, m_prepassLayout);
    makeLayout(m_aoSetLayout, m_aoLayout);
    makeLayout(m_blurSetLayout, m_blurLayout);

    // ---- pre-pass: the scene's meshes, writing normals and depth ----
    {
        VkShaderModule vert = makeModule(device, "shaders/prepass_vert.spv");
        VkShaderModule frag = makeModule(device, "shaders/prepass_frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};

        auto binding = Vertex::getBindingDescription();
        auto attributes = Vertex::getAttributeDescriptions();
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 2;  // position and normal; the pre-pass has no use for texture coordinates
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  // as the scene pass does
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisampling;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &blending;
        info.pDynamicState = &dynamicState;
        info.layout = m_prepassLayout;
        info.renderPass = m_prepassRenderPass;
        const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_prepassPipeline);
        vkDestroyShaderModule(device, frag, nullptr);
        vkDestroyShaderModule(device, vert, nullptr);
        if (result != VK_SUCCESS) throw std::runtime_error("Failed to create pre-pass pipeline!");
    }

    // ---- the two full-screen passes ----
    auto fullscreen = [&](const char* fragPath, VkPipelineLayout layout, VkPipeline& out) {
        VkShaderModule vert = makeModule(device, "shaders/fullscreen_vert.spv");
        VkShaderModule frag = makeModule(device, fragPath);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisampling;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &blending;
        info.pDynamicState = &dynamicState;
        info.layout = layout;
        info.renderPass = m_aoRenderPass;
        const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &out);
        vkDestroyShaderModule(device, frag, nullptr);
        vkDestroyShaderModule(device, vert, nullptr);
        if (result != VK_SUCCESS) throw std::runtime_error(std::string("Failed to create pipeline for ") + fragPath);
    };
    fullscreen("shaders/ssao_frag.spv", m_aoLayout, m_aoPipeline);
    fullscreen("shaders/ssao_blur_frag.spv", m_blurLayout, m_blurPipeline);
}

void AmbientOcclusion::render(VkCommandBuffer commandBuffer, uint32_t frame, Shader& sceneShader, const std::vector<Draw>& draws,
                              const glm::mat4& projection, const Settings& settings) {
    const uint32_t f = frame % kFrames;

    AoUniforms uniforms{};
    uniforms.projection = projection;
    uniforms.inverseProjection = glm::inverse(projection);
    uniforms.params = glm::vec4((std::max)(settings.radius, 0.01f), 0.025f, std::clamp(settings.strength, 0.0f, 1.0f), 2.0f);
    for (int i = 0; i < kSampleCount; i++) uniforms.kernel[i] = m_kernel[static_cast<size_t>(i)];
    std::memcpy(m_uniformMapped[f], &uniforms, sizeof(AoUniforms));

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, m_extent};

    // 1. normals and depth
    {
        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        clears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = m_prepassRenderPass;
        begin.framebuffer = m_prepassFramebuffer;
        begin.renderArea = {{0, 0}, m_extent};
        begin.clearValueCount = static_cast<uint32_t>(clears.size());
        begin.pClearValues = clears.data();
        vkCmdBeginRenderPass(commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_prepassPipeline);
        for (const Draw& draw : draws) {
            if (!draw.mesh) continue;
            sceneShader.bindWithDynamicOffset(commandBuffer, frame, draw.block);  // the object's own uniforms (set 0)
            draw.mesh->draw(commandBuffer);
        }
        vkCmdEndRenderPass(commandBuffer);
    }

    auto fullscreenPass = [&](VkFramebuffer framebuffer, VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet set) {
        VkClearValue clear{};
        clear.color = {{1.0f, 1.0f, 1.0f, 1.0f}};
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = m_aoRenderPass;
        begin.framebuffer = framebuffer;
        begin.renderArea = {{0, 0}, m_extent};
        begin.clearValueCount = 1;
        begin.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    };
    fullscreenPass(m_rawFramebuffer, m_aoPipeline, m_aoLayout, m_aoSets[f]);          // 2. the occlusion
    fullscreenPass(m_blurredFramebuffer, m_blurPipeline, m_blurLayout, m_blurSets[f]);  // 3. blurred
}

} // namespace Propulsion
