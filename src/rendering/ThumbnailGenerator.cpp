#include "rendering/ThumbnailGenerator.h"
#include "rendering/VulkanContext.h"
#include "rendering/Mesh.h"
#include "core/CrashHandler.h"

#ifdef _WIN32
#undef max
#undef min
#endif

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <fstream>
#include <array>
#include <cmath>
#include <cfloat>
#include <algorithm>

namespace Forge {

ThumbnailGenerator::ThumbnailGenerator(VulkanContext* context)
    : m_vulkanContext(context) {
}

ThumbnailGenerator::~ThumbnailGenerator() {
    cleanup();
}

bool ThumbnailGenerator::initialize() {
    if (!createDescriptorResources()) {
        CrashHandler::logError("ThumbnailGenerator: Failed to create descriptor resources");
        return false;
    }
    
    if (!createRenderPass()) {
        CrashHandler::logError("ThumbnailGenerator: Failed to create render pass");
        return false;
    }
    
    if (!createPipeline()) {
        CrashHandler::logError("ThumbnailGenerator: Failed to create pipeline");
        return false;
    }
    
    if (!createUniformBuffer()) {
        CrashHandler::logError("ThumbnailGenerator: Failed to create uniform buffer");
        return false;
    }
    
    CrashHandler::logInfo("ThumbnailGenerator: Initialized successfully");
    return true;
}

std::vector<char> ThumbnailGenerator::readShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    
    if (!file.is_open()) {
        CrashHandler::logError("ThumbnailGenerator: Failed to open shader file: " + filename);
        return {};
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    return buffer;
}

VkShaderModule ThumbnailGenerator::createShaderModule(const std::vector<char>& code) {
    if (code.empty()) return VK_NULL_HANDLE;
    
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_vulkanContext->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    return shaderModule;
}

bool ThumbnailGenerator::createRenderPass() {
    VkDevice device = m_vulkanContext->getDevice();
    
    // Color attachment
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    
    // Depth attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_vulkanContext->findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    
    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

bool ThumbnailGenerator::createDescriptorResources() {
    VkDevice device = m_vulkanContext->getDevice();
    
    // Create descriptor set layout for uniform buffer
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }
    
    // Create descriptor pool - enough for uniform buffer + many thumbnail textures
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 256; // Max thumbnails
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 257; // 1 for uniform + 256 for thumbnails
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

bool ThumbnailGenerator::createPipeline() {
    VkDevice device = m_vulkanContext->getDevice();
    
    // Load shaders
    auto vertShaderCode = readShaderFile("shaders/thumbnail_vert.spv");
    auto fragShaderCode = readShaderFile("shaders/thumbnail_frag.spv");
    
    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        CrashHandler::logError("ThumbnailGenerator: Failed to load shader files");
        return false;
    }
    
    m_vertShaderModule = createShaderModule(vertShaderCode);
    m_fragShaderModule = createShaderModule(fragShaderCode);
    
    if (m_vertShaderModule == VK_NULL_HANDLE || m_fragShaderModule == VK_NULL_HANDLE) {
        CrashHandler::logError("ThumbnailGenerator: Failed to create shader modules");
        return false;
    }
    
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = m_vertShaderModule;
    vertShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = m_fragShaderModule;
    fragShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    
    // Vertex input
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Dynamic viewport and scissor
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Disable culling for FBX compatibility
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Push constant range
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        return false;
    }
    
    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

bool ThumbnailGenerator::createUniformBuffer() {
    VkDevice device = m_vulkanContext->getDevice();
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    
    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_uniformBuffer) != VK_SUCCESS) {
        return false;
    }
    
    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, m_uniformBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(
        memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_uniformBufferMemory) != VK_SUCCESS) {
        return false;
    }
    
    vkBindBufferMemory(device, m_uniformBuffer, m_uniformBufferMemory, 0);
    
    // Allocate descriptor set for uniform buffer
    VkDescriptorSetAllocateInfo allocSetInfo{};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = m_descriptorPool;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &m_descriptorSetLayout;
    
    if (vkAllocateDescriptorSets(device, &allocSetInfo, &m_uniformDescriptorSet) != VK_SUCCESS) {
        return false;
    }
    
    // Update descriptor set
    VkDescriptorBufferInfo bufferDescInfo{};
    bufferDescInfo.buffer = m_uniformBuffer;
    bufferDescInfo.offset = 0;
    bufferDescInfo.range = sizeof(UniformBufferObject);
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_uniformDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferDescInfo;
    
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    
    return true;
}

bool ThumbnailGenerator::createRenderTarget(uint32_t size) {
    if (m_currentSize == size && m_renderImage != VK_NULL_HANDLE) {
        return true; // Already created at this size
    }
    
    cleanupRenderTarget();
    
    VkDevice device = m_vulkanContext->getDevice();
    m_currentSize = size;
    
    // Create color image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = size;
    imageInfo.extent.height = size;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &m_renderImage) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_renderImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_renderImageMemory) != VK_SUCCESS) {
        return false;
    }
    
    vkBindImageMemory(device, m_renderImage, m_renderImageMemory, 0);
    
    // Create color image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_renderImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_renderImageView) != VK_SUCCESS) {
        return false;
    }
    
    // Create depth image
    VkFormat depthFormat = m_vulkanContext->findDepthFormat();
    
    imageInfo.format = depthFormat;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &m_depthImage) != VK_SUCCESS) {
        return false;
    }
    
    vkGetImageMemoryRequirements(device, m_depthImage, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_depthImageMemory) != VK_SUCCESS) {
        return false;
    }
    
    vkBindImageMemory(device, m_depthImage, m_depthImageMemory, 0);
    
    // Create depth image view
    viewInfo.image = m_depthImage;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        return false;
    }
    
    // Create framebuffer
    std::array<VkImageView, 2> attachments = {m_renderImageView, m_depthImageView};
    
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = size;
    framebufferInfo.height = size;
    framebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_framebuffer) != VK_SUCCESS) {
        return false;
    }
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

void ThumbnailGenerator::cleanupRenderTarget() {
    VkDevice device = m_vulkanContext->getDevice();
    
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_depthImageMemory, nullptr);
        m_depthImageMemory = VK_NULL_HANDLE;
    }
    if (m_renderImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_renderImageView, nullptr);
        m_renderImageView = VK_NULL_HANDLE;
    }
    if (m_renderImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_renderImage, nullptr);
        m_renderImage = VK_NULL_HANDLE;
    }
    if (m_renderImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_renderImageMemory, nullptr);
        m_renderImageMemory = VK_NULL_HANDLE;
    }
    
    m_currentSize = 0;
}

void ThumbnailGenerator::calculateIsometricCamera(Mesh* mesh, glm::mat4& view, glm::mat4& proj, float padding) {
    // Calculate tight bounds directly from vertex data
    // This ensures we get actual mesh extents, not stored bounding box
    const auto& vertices = mesh->getVertices();
    
    if (vertices.empty()) {
        // Fallback to unit cube if no vertices
        view = glm::lookAt(glm::vec3(3, 3, 3), glm::vec3(0), glm::vec3(0, 0, 1));
        proj = glm::orthoRH_ZO(-1.5f, 1.5f, -1.5f, 1.5f, 0.1f, 20.0f);
        proj[1][1] *= -1;
        return;
    }
    
    glm::vec3 boundsMin = vertices[0].position;
    glm::vec3 boundsMax = vertices[0].position;
    
    for (const auto& vertex : vertices) {
        boundsMin = glm::min(boundsMin, vertex.position);
        boundsMax = glm::max(boundsMax, vertex.position);
    }
    
    glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
    glm::vec3 extents = (boundsMax - boundsMin) * 0.5f;
    
    CrashHandler::logInfo("Thumbnail tight bounds: min=(" + std::to_string(boundsMin.x) + "," + 
                          std::to_string(boundsMin.y) + "," + std::to_string(boundsMin.z) + 
                          ") max=(" + std::to_string(boundsMax.x) + "," + 
                          std::to_string(boundsMax.y) + "," + std::to_string(boundsMax.z) + ")");
    
    // Ensure we have a minimum size for very small or zero-size meshes
    float maxExtent = std::max({extents.x, extents.y, extents.z});
    if (maxExtent < 0.001f) {
        extents = glm::vec3(1.0f);
        maxExtent = 1.0f;
    }
    
    // Isometric camera direction (equal angles from all axes)
    // Classic isometric: looking from (1, 1, 1) direction normalized
    glm::vec3 isoDir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    
    // Z-up world
    glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
    
    // Calculate camera right and up vectors for the view
    glm::vec3 viewForward = -isoDir;
    glm::vec3 viewRight = glm::normalize(glm::cross(up, viewForward));
    glm::vec3 viewUp = glm::normalize(glm::cross(viewForward, viewRight));
    
    // Project all 8 corners of the bounding box onto the view plane
    // to find the actual screen-space extents
    glm::vec3 corners[8] = {
        boundsMin,
        glm::vec3(boundsMax.x, boundsMin.y, boundsMin.z),
        glm::vec3(boundsMin.x, boundsMax.y, boundsMin.z),
        glm::vec3(boundsMin.x, boundsMin.y, boundsMax.z),
        glm::vec3(boundsMax.x, boundsMax.y, boundsMin.z),
        glm::vec3(boundsMax.x, boundsMin.y, boundsMax.z),
        glm::vec3(boundsMin.x, boundsMax.y, boundsMax.z),
        boundsMax
    };
    
    float minX = FLT_MAX, maxX = -FLT_MAX;
    float minY = FLT_MAX, maxY = -FLT_MAX;
    float minZ = FLT_MAX, maxZ = -FLT_MAX;
    
    for (int i = 0; i < 8; i++) {
        glm::vec3 relPos = corners[i] - center;
        float x = glm::dot(relPos, viewRight);
        float y = glm::dot(relPos, viewUp);
        float z = glm::dot(relPos, viewForward);
        
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
        minZ = std::min(minZ, z);
        maxZ = std::max(maxZ, z);
    }
    
    // Calculate the projected width and height in view space
    float projectedWidth = maxX - minX;
    float projectedHeight = maxY - minY;
    float projectedDepth = maxZ - minZ;
    
    // Use the larger of width/height for the ortho size (to fit in square thumbnail)
    float orthoSize = std::max(projectedWidth, projectedHeight) * 0.5f * (1.0f + padding);
    
    // Position camera far enough to see entire object
    float distance = projectedDepth + orthoSize * 2.0f;
    glm::vec3 cameraPos = center + isoDir * distance;
    
    // Create view matrix
    view = glm::lookAt(cameraPos, center, up);
    
    // For orthographic projection, near/far planes
    float nearPlane = 0.1f;
    float farPlane = distance * 2.0f + projectedDepth;
    
    // Use orthoRH_ZO for Vulkan (Right-Handed, Zero-to-One depth range)
    proj = glm::orthoRH_ZO(-orthoSize, orthoSize, -orthoSize, orthoSize, nearPlane, farPlane);
    
    // Flip Y for Vulkan (Vulkan has Y pointing down in NDC)
    proj[1][1] *= -1;
}

ThumbnailGenerator::ThumbnailResource ThumbnailGenerator::copyToThumbnailImage(uint32_t size) {
    ThumbnailResource resource{};
    VkDevice device = m_vulkanContext->getDevice();
    
    // Create destination image (shader readable)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = size;
    imageInfo.extent.height = size;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &resource.image) != VK_SUCCESS) {
        return resource;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, resource.image, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &resource.memory) != VK_SUCCESS) {
        vkDestroyImage(device, resource.image, nullptr);
        resource.image = VK_NULL_HANDLE;
        return resource;
    }
    
    vkBindImageMemory(device, resource.image, resource.memory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = resource.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &resource.imageView) != VK_SUCCESS) {
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        resource.image = VK_NULL_HANDLE;
        resource.memory = VK_NULL_HANDLE;
        return resource;
    }
    
    // Copy from render image to thumbnail image
    VkCommandBuffer commandBuffer = m_vulkanContext->beginSingleTimeCommands();
    
    // Transition destination to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy
    VkImageCopy copyRegion{};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = {0, 0, 0};
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = {0, 0, 0};
    copyRegion.extent = {size, size, 1};
    
    vkCmdCopyImage(commandBuffer,
        m_renderImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        resource.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copyRegion);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    m_vulkanContext->endSingleTimeCommands(commandBuffer);
    
    // Create ImGui descriptor set
    resource.descriptorSet = ImGui_ImplVulkan_AddTexture(m_sampler, resource.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    return resource;
}

bool ThumbnailGenerator::findContentBounds(uint32_t size, uint32_t& minX, uint32_t& minY, uint32_t& maxX, uint32_t& maxY) {
    VkDevice device = m_vulkanContext->getDevice();
    
    // Create staging buffer to read back pixel data
    VkDeviceSize bufferSize = size * size * 4; // RGBA
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        return false;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        return false;
    }
    
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);
    
    // Copy image to staging buffer
    VkCommandBuffer commandBuffer = m_vulkanContext->beginSingleTimeCommands();
    
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {size, size, 1};
    
    vkCmdCopyImageToBuffer(commandBuffer, m_renderImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);
    
    m_vulkanContext->endSingleTimeCommands(commandBuffer);
    
    // Map and scan pixels
    void* data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    
    uint8_t* pixels = static_cast<uint8_t*>(data);
    
    // Background color is (0.15, 0.15, 0.15) = roughly (38, 38, 38) in 0-255
    const uint8_t bgR = 38, bgG = 38, bgB = 38;
    const int tolerance = 5;
    
    minX = size;
    minY = size;
    maxX = 0;
    maxY = 0;
    
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            uint32_t idx = (y * size + x) * 4;
            uint8_t r = pixels[idx];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];
            
            // Check if pixel is different from background
            if (std::abs((int)r - bgR) > tolerance || 
                std::abs((int)g - bgG) > tolerance || 
                std::abs((int)b - bgB) > tolerance) {
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }
    }
    
    vkUnmapMemory(device, stagingMemory);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    
    // Check if we found any content
    return (maxX >= minX && maxY >= minY);
}

ThumbnailGenerator::ThumbnailResource ThumbnailGenerator::copyToThumbnailImageAutoFit(uint32_t renderSize, uint32_t outputSize, float padding) {
    ThumbnailResource resource{};
    VkDevice device = m_vulkanContext->getDevice();
    
    // Find content bounds
    uint32_t minX, minY, maxX, maxY;
    if (!findContentBounds(renderSize, minX, minY, maxX, maxY)) {
        // No content found, fall back to regular copy
        return copyToThumbnailImage(outputSize);
    }
    
    // Add padding
    uint32_t contentWidth = maxX - minX + 1;
    uint32_t contentHeight = maxY - minY + 1;
    uint32_t maxDim = std::max(contentWidth, contentHeight);
    
    // Calculate padding in pixels
    uint32_t paddingPixels = static_cast<uint32_t>(maxDim * padding);
    
    // Expand bounds with padding, keeping square and centered
    int32_t centerX = (minX + maxX) / 2;
    int32_t centerY = (minY + maxY) / 2;
    int32_t halfSize = (maxDim / 2) + paddingPixels;
    
    int32_t srcMinX = centerX - halfSize;
    int32_t srcMinY = centerY - halfSize;
    int32_t srcMaxX = centerX + halfSize;
    int32_t srcMaxY = centerY + halfSize;
    
    // Clamp to image bounds
    srcMinX = std::max(0, srcMinX);
    srcMinY = std::max(0, srcMinY);
    srcMaxX = std::min((int32_t)renderSize - 1, srcMaxX);
    srcMaxY = std::min((int32_t)renderSize - 1, srcMaxY);
    
    // Create destination image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = outputSize;
    imageInfo.extent.height = outputSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &resource.image) != VK_SUCCESS) {
        return resource;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, resource.image, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &resource.memory) != VK_SUCCESS) {
        vkDestroyImage(device, resource.image, nullptr);
        resource.image = VK_NULL_HANDLE;
        return resource;
    }
    
    vkBindImageMemory(device, resource.image, resource.memory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = resource.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &resource.imageView) != VK_SUCCESS) {
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        resource.image = VK_NULL_HANDLE;
        resource.memory = VK_NULL_HANDLE;
        return resource;
    }
    
    // Blit from render image to thumbnail with scaling
    VkCommandBuffer commandBuffer = m_vulkanContext->beginSingleTimeCommands();
    
    // Transition destination to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Blit with scaling
    VkImageBlit blitRegion{};
    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.mipLevel = 0;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcOffsets[0] = {srcMinX, srcMinY, 0};
    blitRegion.srcOffsets[1] = {srcMaxX + 1, srcMaxY + 1, 1};
    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.mipLevel = 0;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstOffsets[0] = {0, 0, 0};
    blitRegion.dstOffsets[1] = {(int32_t)outputSize, (int32_t)outputSize, 1};
    
    vkCmdBlitImage(commandBuffer,
        m_renderImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        resource.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blitRegion, VK_FILTER_LINEAR);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    m_vulkanContext->endSingleTimeCommands(commandBuffer);
    
    // Create ImGui descriptor set
    resource.descriptorSet = ImGui_ImplVulkan_AddTexture(m_sampler, resource.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    return resource;
}

VkDescriptorSet ThumbnailGenerator::generateMeshThumbnail(Mesh* mesh, uint32_t size) {
    if (!mesh) {
        CrashHandler::logError("ThumbnailGenerator: Null mesh provided");
        return VK_NULL_HANDLE;
    }
    
    // Render at 2x size for better auto-fit detection, then scale down
    uint32_t renderSize = size * 2;
    
    // Ensure render target exists at render size
    if (!createRenderTarget(renderSize)) {
        CrashHandler::logError("ThumbnailGenerator: Failed to create render target");
        return VK_NULL_HANDLE;
    }
    
    VkDevice device = m_vulkanContext->getDevice();
    
    // Calculate camera matrices
    glm::mat4 view, proj;
    calculateIsometricCamera(mesh, view, proj);
    
    // Update uniform buffer
    UniformBufferObject ubo{};
    ubo.view = view;
    ubo.proj = proj;
    
    void* data;
    vkMapMemory(device, m_uniformBufferMemory, 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device, m_uniformBufferMemory);
    
    // Begin single-time command buffer
    VkCommandBuffer commandBuffer = m_vulkanContext->beginSingleTimeCommands();
    
    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {renderSize, renderSize};
    
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.15f, 0.15f, 0.15f, 1.0f}}; // Dark gray background
    clearValues[1].depthStencil = {1.0f, 0};
    
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(renderSize);
    viewport.height = static_cast<float>(renderSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {renderSize, renderSize};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    // Bind pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    
    // Bind descriptor set
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_uniformDescriptorSet, 0, nullptr);
    
    // Set push constants (identity model matrix, nice gray color)
    PushConstants push{};
    push.model = glm::mat4(1.0f);
    push.color = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    
    // Draw mesh
    mesh->draw(commandBuffer);
    
    // End render pass
    vkCmdEndRenderPass(commandBuffer);
    
    // Submit command buffer
    m_vulkanContext->endSingleTimeCommands(commandBuffer);
    
    // Copy result to permanent thumbnail image with auto-fit scaling
    ThumbnailResource thumbnail = copyToThumbnailImageAutoFit(renderSize, size, 0.08f);
    
    if (thumbnail.descriptorSet == VK_NULL_HANDLE) {
        CrashHandler::logError("ThumbnailGenerator: Failed to create thumbnail texture");
        return VK_NULL_HANDLE;
    }
    
    // Store for cleanup
    m_generatedThumbnails.push_back(thumbnail);
    
    CrashHandler::logInfo("ThumbnailGenerator: Generated thumbnail for mesh");
    return thumbnail.descriptorSet;
}

VkDescriptorSet ThumbnailGenerator::generateMaterialThumbnail(const glm::vec3& color, uint32_t size) {
    VkDevice device = m_vulkanContext->getDevice();
    
    ThumbnailResource resource{};
    
    // Create image filled with solid color
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = size;
    imageInfo.extent.height = size;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &resource.image) != VK_SUCCESS) {
        CrashHandler::logError("ThumbnailGenerator: Failed to create material thumbnail image");
        return VK_NULL_HANDLE;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, resource.image, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &resource.memory) != VK_SUCCESS) {
        vkDestroyImage(device, resource.image, nullptr);
        CrashHandler::logError("ThumbnailGenerator: Failed to allocate material thumbnail memory");
        return VK_NULL_HANDLE;
    }
    
    vkBindImageMemory(device, resource.image, resource.memory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = resource.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &resource.imageView) != VK_SUCCESS) {
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        CrashHandler::logError("ThumbnailGenerator: Failed to create material thumbnail image view");
        return VK_NULL_HANDLE;
    }
    
    // Create staging buffer with pixel data
    VkDeviceSize bufferSize = size * size * 4;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        vkDestroyImageView(device, resource.imageView, nullptr);
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        return VK_NULL_HANDLE;
    }
    
    VkMemoryRequirements bufferMemReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &bufferMemReqs);
    
    VkMemoryAllocateInfo bufferAllocInfo{};
    bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufferAllocInfo.allocationSize = bufferMemReqs.size;
    bufferAllocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(bufferMemReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &bufferAllocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkDestroyImageView(device, resource.imageView, nullptr);
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        return VK_NULL_HANDLE;
    }
    
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);
    
    // Fill staging buffer with solid color
    void* data;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
    
    uint8_t* pixels = static_cast<uint8_t*>(data);
    uint8_t r = static_cast<uint8_t>(color.r * 255.0f);
    uint8_t g = static_cast<uint8_t>(color.g * 255.0f);
    uint8_t b = static_cast<uint8_t>(color.b * 255.0f);
    uint8_t a = 255;
    
    for (uint32_t i = 0; i < size * size; i++) {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }
    
    vkUnmapMemory(device, stagingMemory);
    
    // Copy staging buffer to image
    VkCommandBuffer commandBuffer = m_vulkanContext->beginSingleTimeCommands();
    
    // Transition image to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {size, size, 1};
    
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, resource.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    m_vulkanContext->endSingleTimeCommands(commandBuffer);
    
    // Cleanup staging buffer
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    
    // Create ImGui descriptor set
    resource.descriptorSet = ImGui_ImplVulkan_AddTexture(m_sampler, resource.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    if (resource.descriptorSet == VK_NULL_HANDLE) {
        vkDestroyImageView(device, resource.imageView, nullptr);
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        CrashHandler::logError("ThumbnailGenerator: Failed to create material thumbnail descriptor");
        return VK_NULL_HANDLE;
    }
    
    // Store for cleanup
    m_generatedThumbnails.push_back(resource);
    
    return resource.descriptorSet;
}

VkDescriptorSet ThumbnailGenerator::generateTextureThumbnail(const unsigned char* data, int width, int height, uint32_t size) {
    VkDevice device = m_vulkanContext->getDevice();
    
    ThumbnailResource resource{};
    
    // Create thumbnail image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = size;
    imageInfo.extent.height = size;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &resource.image) != VK_SUCCESS) {
        CrashHandler::logError("ThumbnailGenerator: Failed to create texture thumbnail image");
        return VK_NULL_HANDLE;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, resource.image, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &resource.memory) != VK_SUCCESS) {
        vkDestroyImage(device, resource.image, nullptr);
        CrashHandler::logError("ThumbnailGenerator: Failed to allocate texture thumbnail memory");
        return VK_NULL_HANDLE;
    }
    
    vkBindImageMemory(device, resource.image, resource.memory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = resource.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &resource.imageView) != VK_SUCCESS) {
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        CrashHandler::logError("ThumbnailGenerator: Failed to create texture thumbnail image view");
        return VK_NULL_HANDLE;
    }
    
    // Create staging buffer with resized image data
    VkDeviceSize bufferSize = size * size * 4;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        vkDestroyImageView(device, resource.imageView, nullptr);
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        return VK_NULL_HANDLE;
    }
    
    VkMemoryRequirements bufferMemReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &bufferMemReqs);
    
    VkMemoryAllocateInfo bufferAllocInfo{};
    bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufferAllocInfo.allocationSize = bufferMemReqs.size;
    bufferAllocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(bufferMemReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &bufferAllocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkDestroyImageView(device, resource.imageView, nullptr);
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        return VK_NULL_HANDLE;
    }
    
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);
    
    // Fill staging buffer with resampled image data
    void* mappedData;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &mappedData);
    
    uint8_t* pixels = static_cast<uint8_t*>(mappedData);
    
    // Simple nearest-neighbor resize
    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            // Calculate source pixel position
            int srcX = static_cast<int>((float)x / size * width);
            int srcY = static_cast<int>((float)y / size * height);
            srcX = std::min(srcX, width - 1);
            srcY = std::min(srcY, height - 1);
            
            // Copy pixel (RGBA)
            int srcIndex = (srcY * width + srcX) * 4;
            int dstIndex = (y * size + x) * 4;
            
            pixels[dstIndex + 0] = data[srcIndex + 0];
            pixels[dstIndex + 1] = data[srcIndex + 1];
            pixels[dstIndex + 2] = data[srcIndex + 2];
            pixels[dstIndex + 3] = data[srcIndex + 3];
        }
    }
    
    vkUnmapMemory(device, stagingMemory);
    
    // Copy staging buffer to image
    VkCommandBuffer commandBuffer = m_vulkanContext->beginSingleTimeCommands();
    
    // Transition image to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {size, size, 1};
    
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, resource.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    m_vulkanContext->endSingleTimeCommands(commandBuffer);
    
    // Cleanup staging buffer
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    
    // Create ImGui descriptor set
    resource.descriptorSet = ImGui_ImplVulkan_AddTexture(m_sampler, resource.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    if (resource.descriptorSet == VK_NULL_HANDLE) {
        vkDestroyImageView(device, resource.imageView, nullptr);
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        CrashHandler::logError("ThumbnailGenerator: Failed to create texture thumbnail descriptor");
        return VK_NULL_HANDLE;
    }
    
    // Store for cleanup
    m_generatedThumbnails.push_back(resource);
    
    CrashHandler::logInfo("ThumbnailGenerator: Generated texture thumbnail (" + std::to_string(width) + "x" + std::to_string(height) + " -> " + std::to_string(size) + "x" + std::to_string(size) + ")");
    return resource.descriptorSet;
}

VkDescriptorSet ThumbnailGenerator::createThumbnailFromPixels(const unsigned char* pixels, uint32_t width, uint32_t height) {
    if (!pixels || width == 0 || height == 0) {
        return VK_NULL_HANDLE;
    }
    
    VkDevice device = m_vulkanContext->getDevice();
    ThumbnailResource resource{};
    
    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &resource.image) != VK_SUCCESS) {
        CrashHandler::logError("ThumbnailGenerator: Failed to create thumbnail image from pixels");
        return VK_NULL_HANDLE;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, resource.image, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &resource.memory) != VK_SUCCESS) {
        vkDestroyImage(device, resource.image, nullptr);
        CrashHandler::logError("ThumbnailGenerator: Failed to allocate thumbnail memory from pixels");
        return VK_NULL_HANDLE;
    }
    
    vkBindImageMemory(device, resource.image, resource.memory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = resource.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &resource.imageView) != VK_SUCCESS) {
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        CrashHandler::logError("ThumbnailGenerator: Failed to create thumbnail image view from pixels");
        return VK_NULL_HANDLE;
    }
    
    // Create staging buffer
    VkDeviceSize bufferSize = width * height * 4;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        vkDestroyImageView(device, resource.imageView, nullptr);
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        return VK_NULL_HANDLE;
    }
    
    VkMemoryRequirements bufferMemReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &bufferMemReqs);
    
    VkMemoryAllocateInfo bufferAllocInfo{};
    bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufferAllocInfo.allocationSize = bufferMemReqs.size;
    bufferAllocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(bufferMemReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &bufferAllocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkDestroyImageView(device, resource.imageView, nullptr);
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        return VK_NULL_HANDLE;
    }
    
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);
    
    // Copy pixel data to staging buffer
    void* mappedData;
    vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &mappedData);
    memcpy(mappedData, pixels, bufferSize);
    vkUnmapMemory(device, stagingMemory);
    
    // Copy staging buffer to image
    VkCommandBuffer commandBuffer = m_vulkanContext->beginSingleTimeCommands();
    
    // Transition to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resource.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, resource.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    m_vulkanContext->endSingleTimeCommands(commandBuffer);
    
    // Cleanup staging buffer
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    
    // Create ImGui descriptor set
    resource.descriptorSet = ImGui_ImplVulkan_AddTexture(m_sampler, resource.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    if (resource.descriptorSet == VK_NULL_HANDLE) {
        vkDestroyImageView(device, resource.imageView, nullptr);
        vkFreeMemory(device, resource.memory, nullptr);
        vkDestroyImage(device, resource.image, nullptr);
        CrashHandler::logError("ThumbnailGenerator: Failed to create descriptor from pixels");
        return VK_NULL_HANDLE;
    }
    
    // Store for cleanup
    m_generatedThumbnails.push_back(resource);
    
    CrashHandler::logInfo("ThumbnailGenerator: Created thumbnail from pixels (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    return resource.descriptorSet;
}

void ThumbnailGenerator::releaseThumbnail(VkDescriptorSet thumbnail) {
    if (thumbnail == VK_NULL_HANDLE) return;
    
    VkDevice device = m_vulkanContext->getDevice();
    
    for (auto it = m_generatedThumbnails.begin(); it != m_generatedThumbnails.end(); ++it) {
        if (it->descriptorSet == thumbnail) {
            // Remove from ImGui
            ImGui_ImplVulkan_RemoveTexture(it->descriptorSet);
            
            // Cleanup Vulkan resources
            if (it->imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, it->imageView, nullptr);
            }
            if (it->image != VK_NULL_HANDLE) {
                vkDestroyImage(device, it->image, nullptr);
            }
            if (it->memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, it->memory, nullptr);
            }
            
            m_generatedThumbnails.erase(it);
            break;
        }
    }
}

void ThumbnailGenerator::releaseAllThumbnails() {
    VkDevice device = m_vulkanContext->getDevice();
    
    for (auto& thumbnail : m_generatedThumbnails) {
        if (thumbnail.descriptorSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(thumbnail.descriptorSet);
        }
        if (thumbnail.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, thumbnail.imageView, nullptr);
        }
        if (thumbnail.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, thumbnail.image, nullptr);
        }
        if (thumbnail.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, thumbnail.memory, nullptr);
        }
    }
    
    m_generatedThumbnails.clear();
}

void ThumbnailGenerator::cleanup() {
    VkDevice device = m_vulkanContext->getDevice();
    
    // Wait for device to be idle
    vkDeviceWaitIdle(device);
    
    // Release all thumbnails
    releaseAllThumbnails();
    
    // Cleanup render target
    cleanupRenderTarget();
    
    // Cleanup uniform buffer
    if (m_uniformBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_uniformBuffer, nullptr);
        m_uniformBuffer = VK_NULL_HANDLE;
    }
    if (m_uniformBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_uniformBufferMemory, nullptr);
        m_uniformBufferMemory = VK_NULL_HANDLE;
    }
    
    // Cleanup pipeline
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    
    // Cleanup shader modules
    if (m_vertShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, m_vertShaderModule, nullptr);
        m_vertShaderModule = VK_NULL_HANDLE;
    }
    if (m_fragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, m_fragShaderModule, nullptr);
        m_fragShaderModule = VK_NULL_HANDLE;
    }
    
    // Cleanup render pass
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    
    // Cleanup descriptor resources
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    
    CrashHandler::logInfo("ThumbnailGenerator: Cleaned up all resources");
}

} // namespace Forge
