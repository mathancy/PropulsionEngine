#include "rendering/ShadowRenderer.h"
#include "rendering/VulkanContext.h"
#include "core/CrashHandler.h"

#include <array>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Propulsion {

namespace {

// Matches `Push` in shaders/shadow.vert.
struct ShadowPush {
    glm::mat4 lightViewProjection;
    glm::mat4 model;
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

constexpr uint32_t kPointLayers = static_cast<uint32_t>(Shadows::kMaxPointShadows * Shadows::kFacesPerPoint);

} // namespace

ShadowRenderer::ShadowRenderer(VulkanContext* context) : m_context(context) {
    VkDevice device = m_context->getDevice();
    m_format = m_context->findSupportedFormat({VK_FORMAT_D32_SFLOAT}, VK_IMAGE_TILING_OPTIMAL,
                                              VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    createRenderPass();
    createPipeline();

    // The sun's maps: an array of layers, one for each cascade. The point lights' faces: an array of layers, six for each light.
    createTarget(m_directional, static_cast<uint32_t>(Shadows::kCascadeMapSize), static_cast<uint32_t>(Shadows::kCascadeCount));
    createTarget(m_points, static_cast<uint32_t>(Shadows::kPointMapSize), kPointLayers);

    auto makeView = [&](VkImage image, VkImageViewType type, uint32_t baseLayer, uint32_t layers) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = type;
        info.format = m_format;
        info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, baseLayer, layers};
        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS) throw std::runtime_error("Failed to create shadow map view!");
        return view;
    };
    auto makeFramebuffer = [&](VkImageView view, uint32_t size) {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = m_renderPass;
        info.attachmentCount = 1;
        info.pAttachments = &view;
        info.width = size;
        info.height = size;
        info.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(device, &info, nullptr, &framebuffer) != VK_SUCCESS) throw std::runtime_error("Failed to create shadow framebuffer!");
        return framebuffer;
    };

    m_directionalArrayView = makeView(m_directional.image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, static_cast<uint32_t>(Shadows::kCascadeCount));
    for (uint32_t layer = 0; layer < static_cast<uint32_t>(Shadows::kCascadeCount); layer++) {
        m_directionalLayerViews.push_back(makeView(m_directional.image, VK_IMAGE_VIEW_TYPE_2D, layer, 1));
        m_directionalFramebuffers.push_back(makeFramebuffer(m_directionalLayerViews.back(), static_cast<uint32_t>(Shadows::kCascadeMapSize)));
    }
    m_pointArrayView = makeView(m_points.image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, kPointLayers);
    for (uint32_t layer = 0; layer < kPointLayers; layer++) {
        m_pointLayerViews.push_back(makeView(m_points.image, VK_IMAGE_VIEW_TYPE_2D, layer, 1));
        m_pointFramebuffers.push_back(makeFramebuffer(m_pointLayerViews.back(), static_cast<uint32_t>(Shadows::kPointMapSize)));
    }

    // The shader compares depths itself (to filter them softly), so it reads them as plain numbers, nearest.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) throw std::runtime_error("Failed to create shadow sampler!");

    clearAll();  // every map starts as "nothing in the way", already in the layout the shader reads it in
    CrashHandler::logInfo("Shadow renderer created: sun " + std::to_string(Shadows::kCascadeCount) + " cascades of " +
                          std::to_string(Shadows::kCascadeMapSize) + "x" + std::to_string(Shadows::kCascadeMapSize) + ", " + std::to_string(Shadows::kMaxPointShadows) +
                          " point lights x 6 faces of " + std::to_string(Shadows::kPointMapSize));
}

ShadowRenderer::~ShadowRenderer() {
    VkDevice device = m_context->getDevice();
    vkDeviceWaitIdle(device);
    for (VkFramebuffer fb : m_pointFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    for (VkImageView view : m_pointLayerViews) vkDestroyImageView(device, view, nullptr);
    if (m_pointArrayView != VK_NULL_HANDLE) vkDestroyImageView(device, m_pointArrayView, nullptr);
    for (VkFramebuffer fb : m_directionalFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    for (VkImageView view : m_directionalLayerViews) vkDestroyImageView(device, view, nullptr);
    if (m_directionalArrayView != VK_NULL_HANDLE) vkDestroyImageView(device, m_directionalArrayView, nullptr);
    for (Target* t : {&m_points, &m_directional}) {
        if (t->image != VK_NULL_HANDLE) vkDestroyImage(device, t->image, nullptr);
        if (t->memory != VK_NULL_HANDLE) vkFreeMemory(device, t->memory, nullptr);
    }
    if (m_sampler != VK_NULL_HANDLE) vkDestroySampler(device, m_sampler, nullptr);
    if (m_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, m_renderPass, nullptr);
}

void ShadowRenderer::createTarget(Target& target, uint32_t size, uint32_t layers) {
    VkDevice device = m_context->getDevice();
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = m_format;
    info.extent = {size, size, 1};
    info.mipLevels = 1;
    info.arrayLayers = layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &info, nullptr, &target.image) != VK_SUCCESS) throw std::runtime_error("Failed to create shadow map!");

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, target.image, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = m_context->findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &alloc, nullptr, &target.memory) != VK_SUCCESS) throw std::runtime_error("Failed to allocate shadow map memory!");
    vkBindImageMemory(device, target.image, target.memory, 0);
}

void ShadowRenderer::createRenderPass() {
    VkAttachmentDescription depth{};
    depth.format = m_format;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // it is cleared, so what was there doesn't matter
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    // Before: the last frame's scene pass may still be reading these maps. After: the scene pass reads what was drawn.
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &depth;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();
    if (vkCreateRenderPass(m_context->getDevice(), &info, nullptr, &m_renderPass) != VK_SUCCESS) throw std::runtime_error("Failed to create shadow render pass!");
}

void ShadowRenderer::createPipeline() {
    VkDevice device = m_context->getDevice();
    const std::vector<char> code = readFile("shaders/shadow_vert.spv");
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule vert = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &vert) != VK_SUCCESS) throw std::runtime_error("Failed to create shadow shader module!");

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = vert;
    stage.pName = "main";

    auto binding = Vertex::getBindingDescription();
    auto attributes = Vertex::getAttributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;  // only the position
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Both sides of every surface are drawn (a thin floor or an open mesh must still cast), with a little slope-scaled
    // bias so a surface doesn't shadow itself; the shader adds an offset along the normal on top.
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendStateCreateInfo blending{};  // no color attachment
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 3;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush)};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) throw std::runtime_error("Failed to create shadow pipeline layout!");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &stage;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
    vkDestroyShaderModule(device, vert, nullptr);
    if (result != VK_SUCCESS) throw std::runtime_error("Failed to create shadow pipeline!");
}

void ShadowRenderer::clearAll() {
    VkCommandBuffer commandBuffer = m_context->beginSingleTimeCommands();
    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};
    auto clearOne = [&](VkFramebuffer framebuffer, uint32_t size) {
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = m_renderPass;
        begin.framebuffer = framebuffer;
        begin.renderArea = {{0, 0}, {size, size}};
        begin.clearValueCount = 1;
        begin.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(commandBuffer);
    };
    for (VkFramebuffer fb : m_directionalFramebuffers) clearOne(fb, static_cast<uint32_t>(Shadows::kCascadeMapSize));
    for (VkFramebuffer fb : m_pointFramebuffers) clearOne(fb, static_cast<uint32_t>(Shadows::kPointMapSize));
    m_context->endSingleTimeCommands(commandBuffer);
}

void ShadowRenderer::drawCasters(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, uint32_t size, const glm::mat4& viewProjection,
                                 const std::vector<Caster>& casters, const glm::vec3* lightPosition, float lightReach,
                                 const Shadows::DirectionalShadow* map) {
    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = m_renderPass;
    begin.framebuffer = framebuffer;
    begin.renderArea = {{0, 0}, {size, size}};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {size, size}};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdSetDepthBias(commandBuffer, 1.25f, 0.0f, 1.75f);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    for (const Caster& caster : casters) {
        if (!caster.mesh) continue;
        // A point light only shades what it reaches.
        if (lightPosition && glm::length(caster.center - *lightPosition) > lightReach + caster.radius) continue;
        // A cascade only shades the part of the world it covers.
        if (map && !Shadows::sphereOverlapsMap(*map, caster.center, caster.radius)) continue;
        ShadowPush push{viewProjection, caster.model};
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush), &push);
        caster.mesh->draw(commandBuffer);
    }
    vkCmdEndRenderPass(commandBuffer);
}

void ShadowRenderer::render(VkCommandBuffer commandBuffer, const ShadowFrame& frame, const std::vector<Caster>& casters) {
    if (frame.directional.valid) {
        for (int cascade = 0; cascade < frame.directional.count; cascade++) {
            const Shadows::DirectionalShadow& map = frame.directional.maps[static_cast<size_t>(cascade)];
            drawCasters(commandBuffer, m_directionalFramebuffers[static_cast<size_t>(cascade)], static_cast<uint32_t>(Shadows::kCascadeMapSize),
                        map.viewProjection, casters, nullptr, 0.0f, &map);
        }
    }
    for (int slot = 0; slot < frame.pointCount; slot++) {
        for (int face = 0; face < Shadows::kFacesPerPoint; face++) {
            drawCasters(commandBuffer, m_pointFramebuffers[static_cast<size_t>(slot * Shadows::kFacesPerPoint + face)],
                        static_cast<uint32_t>(Shadows::kPointMapSize), frame.pointFaces[slot][face], casters, &frame.pointPosition[slot],
                        frame.pointReach[slot], nullptr);
        }
    }
}

} // namespace Propulsion
