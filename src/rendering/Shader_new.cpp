#include "rendering/Shader.h"
#include "rendering/VulkanContext.h"
#include "rendering/Mesh.h"
#include "core/CrashHandler.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <array>
#include <memory>

namespace Propulsion {

Shader::Shader(VulkanContext* context, const char* vertexPath, const char* fragmentPath, VkRenderPass renderPass, VkCullModeFlags cullMode, bool depthTestEnable, bool supportsTextures, VkDescriptorSetLayout extraSetLayout, VkDescriptorSetLayout bindlessSetLayout)
    : m_context(context), m_pipeline(VK_NULL_HANDLE), m_pipelineLayout(VK_NULL_HANDLE), 
      m_descriptorSetLayout(VK_NULL_HANDLE), m_extraSetLayout(extraSetLayout), m_cullMode(cullMode), m_depthTestEnable(depthTestEnable),
      m_usesPushConstants(false), m_supportsTextures(supportsTextures) {
    if (bindlessSetLayout != VK_NULL_HANDLE) {
        if (extraSetLayout == VK_NULL_HANDLE) throw std::runtime_error("A shader with a texture table needs the lighting set too");
        m_bindlessSetLayout = bindlessSetLayout;
        m_dynamicCull = true;
    }
    
    if (m_supportsTextures) {
        createDefaultTexture();
        createTexturedDescriptorSetLayout();
    } else {
        createDescriptorSetLayout();
    }
    // Use custom render pass if provided, otherwise use the main render pass
    VkRenderPass targetRenderPass = (renderPass != VK_NULL_HANDLE) ? renderPass : m_context->getRenderPass();
    createGraphicsPipeline(vertexPath, fragmentPath, targetRenderPass);
    createUniformBuffers();
    if (m_supportsTextures) {
        createTexturedDescriptorSets();
    } else {
        createDescriptorSets();
    }
}

// Private constructor for gizmo shader with push constants and MSAA
Shader::Shader(VulkanContext* context, const char* vertexPath, const char* fragmentPath, VkRenderPass renderPass, bool usesPushConstants, VkSampleCountFlagBits msaaSamples)
    : m_context(context), m_pipeline(VK_NULL_HANDLE), m_pipelineLayout(VK_NULL_HANDLE), 
      m_descriptorSetLayout(VK_NULL_HANDLE), m_cullMode(VK_CULL_MODE_NONE), m_depthTestEnable(false),
      m_usesPushConstants(usesPushConstants), m_msaaSamples(msaaSamples) {
    
    createGizmoDescriptorSetLayout();
    createGizmoPipeline(vertexPath, fragmentPath, renderPass);
    createGizmoUniformBuffers();
    createGizmoDescriptorSets();
}

std::unique_ptr<Shader> Shader::createGizmoShader(VulkanContext* context, const char* vertexPath, const char* fragmentPath, VkRenderPass renderPass, VkSampleCountFlagBits msaaSamples) {
    return std::unique_ptr<Shader>(new Shader(context, vertexPath, fragmentPath, renderPass, true, msaaSamples));
}

Shader::~Shader() {
    VkDevice device = m_context->getDevice();
    
    for (size_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(device, m_uniformBuffers[i], nullptr);
        vkFreeMemory(device, m_uniformBuffersMemory[i], nullptr);
    }
    
    // Clean up texture resources if this shader supports textures
    if (m_supportsTextures) {
        if (m_defaultSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, m_defaultSampler, nullptr);
        }
        if (m_defaultTextureView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_defaultTextureView, nullptr);
        }
        if (m_defaultTexture != VK_NULL_HANDLE) {
            vkDestroyImage(device, m_defaultTexture, nullptr);
        }
        if (m_defaultTextureMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_defaultTextureMemory, nullptr);
        }
    }
    
    vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    for (VkPipeline variant : m_variants) vkDestroyPipeline(device, variant, nullptr);
    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
}

void Shader::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;  // one block per object, picked with a dynamic offset
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(m_context->getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }
}

void Shader::createTexturedDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
    
    // Binding 0: Uniform buffer
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;  // one block per object, picked with a dynamic offset
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;
    
    // Binding 1: Combined image sampler for albedo texture
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;
    
    // Binding 2: Combined image sampler for metallic texture
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;
    
    // Binding 3: Combined image sampler for roughness texture
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[3].pImmutableSamplers = nullptr;
    
    // Binding 4: Combined image sampler for normal texture
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[4].pImmutableSamplers = nullptr;
    
    // Binding 5: Combined image sampler for ambient occlusion texture
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[5].pImmutableSamplers = nullptr;
    
    // Binding 6: Combined image sampler for emissive texture
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[6].pImmutableSamplers = nullptr;
    
    // Binding 7: Combined image sampler for height texture (parallax mapping)
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[7].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_context->getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create textured descriptor set layout!");
    }
}

int Shader::addVariant(const std::string& fragmentPath, VkRenderPass renderPass, bool blended) {
    VkPipeline pipeline = VK_NULL_HANDLE;
    createGraphicsPipeline(m_vertexPath, fragmentPath, renderPass, &pipeline, blended);
    m_variants.push_back(pipeline);
    return static_cast<int>(m_variants.size());
}

void Shader::setCullMode(VkCommandBuffer commandBuffer, bool twoSided) const {
    if (m_dynamicCull) vkCmdSetCullMode(commandBuffer, twoSided ? VK_CULL_MODE_NONE : m_cullMode);
}

void Shader::createGraphicsPipeline(const std::string& vertexPath, const std::string& fragmentPath, VkRenderPass renderPass, VkPipeline* variantOut, bool blended) {
    m_vertexPath = vertexPath;
    CrashHandler::logInfo("Loading vertex shader: " + vertexPath);
    auto vertShaderCode = readFile(vertexPath);
    CrashHandler::logInfo("Vertex shader size: " + std::to_string(vertShaderCode.size()) + " bytes");
    
    CrashHandler::logInfo("Loading fragment shader: " + fragmentPath);
    auto fragShaderCode = readFile(fragmentPath);
    CrashHandler::logInfo("Fragment shader size: " + std::to_string(fragShaderCode.size()) + " bytes");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

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

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_context->getSwapChainExtent().width;
    viewport.height = (float)m_context->getSwapChainExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_context->getSwapChainExtent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = m_cullMode;  // Use the cull mode passed in constructor
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = (m_depthTestEnable && !blended) ? VK_TRUE : VK_FALSE;  // see-through surfaces test depth but don't hide what is behind
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = blended ? VK_TRUE : VK_FALSE;
    // The shader's colour is already multiplied by its alpha: add it to what is behind, which is dimmed by the alpha
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;  // the picture's own alpha stays as it was
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    // Set 0 is the shader's own (per-object uniforms and textures); set 1, if there is one, is the lighting.
    // Set 2, if there is one, is the table of every material texture.
    VkDescriptorSetLayout setLayouts[3] = {m_descriptorSetLayout, m_extraSetLayout, m_bindlessSetLayout};
    pipelineLayoutInfo.setLayoutCount = (m_bindlessSetLayout != VK_NULL_HANDLE) ? 3 : (m_extraSetLayout != VK_NULL_HANDLE) ? 2 : 1;
    pipelineLayoutInfo.pSetLayouts = setLayouts;

    if (!variantOut && vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout!");
    }

    // Enable dynamic viewport and scissor so they can be set per-frame
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    if (m_dynamicCull) dynamicStates.push_back(VK_DYNAMIC_STATE_CULL_MODE);

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

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
    pipelineInfo.renderPass = renderPass;  // Use the provided render pass
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline* target = variantOut ? variantOut : &m_pipeline;
    const VkResult created = vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, target);
    if (created != VK_SUCCESS) {
        vkDestroyShaderModule(m_context->getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(m_context->getDevice(), vertShaderModule, nullptr);
        throw std::runtime_error("Failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(m_context->getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(m_context->getDevice(), vertShaderModule, nullptr);
    
    CrashHandler::logInfo("Graphics pipeline created successfully");
}

void Shader::createUniformBuffers() {
    // One block per object, each starting at a multiple of the device's uniform-offset alignment,
    // so a draw can pick its block with a dynamic offset.
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(m_context->getPhysicalDevice(), &deviceProperties);
    const VkDeviceSize minAlignment = (std::max<VkDeviceSize>)(deviceProperties.limits.minUniformBufferOffsetAlignment, 1);
    m_dynamicAlignment = static_cast<uint32_t>((sizeof(UniformBufferObject) + minAlignment - 1) & ~(minAlignment - 1));
    m_dynamicBufferSize = static_cast<VkDeviceSize>(m_dynamicAlignment) * MAX_OBJECTS;
    VkDeviceSize bufferSize = m_dynamicBufferSize;

    m_uniformBuffers.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_context->getDevice(), &bufferInfo, nullptr, &m_uniformBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create uniform buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_context->getDevice(), m_uniformBuffers[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = m_context->findMemoryType(memRequirements.memoryTypeBits, 
                                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &m_uniformBuffersMemory[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate uniform buffer memory!");
        }

        vkBindBufferMemory(m_context->getDevice(), m_uniformBuffers[i], m_uniformBuffersMemory[i], 0);
        vkMapMemory(m_context->getDevice(), m_uniformBuffersMemory[i], 0, bufferSize, 0, &m_uniformBuffersMapped[i]);
    }
}

void Shader::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(VulkanContext::MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_context->getDescriptorPool();
    allocInfo.descriptorSetCount = static_cast<uint32_t>(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        // For dynamic UBO, range is the size of ONE UBO (alignment doesn't affect range)
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_context->getDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

void Shader::createTexturedDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(VulkanContext::MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_context->getDescriptorPool();
    allocInfo.descriptorSetCount = static_cast<uint32_t>(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate textured descriptor sets!");
    }

    for (size_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        // For dynamic UBO, range is the size of ONE UBO
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_defaultTextureView;
        imageInfo.sampler = m_defaultSampler;

        // 8 descriptor writes: 1 UBO + 7 texture samplers (including height)
        std::array<VkWriteDescriptorSet, 8> descriptorWrites{};
        
        // Binding 0: Dynamic UBO
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        // Initialize all texture bindings to default texture (bindings 1-7)
        for (int j = 1; j <= 7; j++) {
            descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j].dstSet = m_descriptorSets[i];
            descriptorWrites[j].dstBinding = j;
            descriptorWrites[j].dstArrayElement = 0;
            descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[j].descriptorCount = 1;
            descriptorWrites[j].pImageInfo = &imageInfo;
        }

        vkUpdateDescriptorSets(m_context->getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void Shader::createDefaultTexture() {
    // Create a 1x1 white texture as default
    VkDevice device = m_context->getDevice();
    
    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = 1;
    imageInfo.extent.height = 1;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &m_defaultTexture) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create default texture image!");
    }
    
    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_defaultTexture, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_context->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_defaultTextureMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate default texture memory!");
    }
    
    vkBindImageMemory(device, m_defaultTexture, m_defaultTextureMemory, 0);
    
    // Create staging buffer with white pixel
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VkDeviceSize imageSize = 4; // 1 pixel * 4 bytes (RGBA)
    
    // Create staging buffer manually
    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create staging buffer for default texture!");
    }
    
    VkMemoryRequirements stagingMemRequirements;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingMemRequirements);
    
    VkMemoryAllocateInfo stagingAllocInfo{};
    stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocInfo.allocationSize = stagingMemRequirements.size;
    stagingAllocInfo.memoryTypeIndex = m_context->findMemoryType(stagingMemRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &stagingAllocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate staging buffer memory for default texture!");
    }
    
    vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);
    
    // Write white pixel (255, 255, 255, 255)
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    uint8_t whitePixel[4] = {255, 255, 255, 255};
    memcpy(data, whitePixel, 4);
    vkUnmapMemory(device, stagingBufferMemory);
    
    // Transition image layout and copy data
    VkCommandBuffer commandBuffer = m_context->beginSingleTimeCommands();
    
    // Transition to transfer destination
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_defaultTexture;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
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
    region.imageExtent = {1, 1, 1};
    
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_defaultTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    m_context->endSingleTimeCommands(commandBuffer);
    
    // Clean up staging buffer
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_defaultTexture;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_defaultTextureView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create default texture image view!");
    }
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_defaultSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create default texture sampler!");
    }
}

void Shader::bindTexture(uint32_t currentFrame, VkImageView textureView, VkSampler sampler, uint32_t binding) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = textureView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSets[currentFrame];
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_context->getDevice(), 1, &descriptorWrite, 0, nullptr);
}

void Shader::bindPBRTextures(uint32_t currentFrame,
                             VkImageView albedoView, VkSampler albedoSampler,
                             VkImageView metallicView, VkSampler metallicSampler,
                             VkImageView roughnessView, VkSampler roughnessSampler,
                             VkImageView normalView, VkSampler normalSampler,
                             VkImageView aoView, VkSampler aoSampler,
                             VkImageView emissiveView, VkSampler emissiveSampler) {
    std::array<VkDescriptorImageInfo, 6> imageInfos{};
    
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = albedoView;
    imageInfos[0].sampler = albedoSampler;
    
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = metallicView;
    imageInfos[1].sampler = metallicSampler;
    
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[2].imageView = roughnessView;
    imageInfos[2].sampler = roughnessSampler;
    
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[3].imageView = normalView;
    imageInfos[3].sampler = normalSampler;
    
    imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[4].imageView = aoView;
    imageInfos[4].sampler = aoSampler;
    
    imageInfos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[5].imageView = emissiveView;
    imageInfos[5].sampler = emissiveSampler;

    std::array<VkWriteDescriptorSet, 6> descriptorWrites{};
    for (int i = 0; i < 6; i++) {
        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = m_descriptorSets[currentFrame];
        descriptorWrites[i].dstBinding = i + 1; // Bindings 1-6
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(m_context->getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Shader::unbindTexture(uint32_t currentFrame) {
    // Reset albedo to default white texture
    bindTexture(currentFrame, m_defaultTextureView, m_defaultSampler, 1);
}

void Shader::unbindAllTextures(uint32_t currentFrame) {
    // Reset all texture bindings to default white texture
    for (uint32_t binding = 1; binding <= 6; binding++) {
        bindTexture(currentFrame, m_defaultTextureView, m_defaultSampler, binding);
    }
}

void Shader::bind(VkCommandBuffer commandBuffer, int variant) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, hasVariant(variant) ? m_variants[variant - 1] : m_pipeline);
    if (m_dynamicCull) vkCmdSetCullMode(commandBuffer, m_cullMode);  // the normal faces; a two-sided draw says so with setCullMode
    
    // Bind descriptor sets containing uniform buffers (MVP matrices and color)
    uint32_t currentFrame = m_context->getCurrentFrame();
    if (m_dynamicAlignment > 0) {
        // The uniform buffer binding is dynamic, so a bind has to give an offset: the first block.
        uint32_t firstBlock = 0;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_descriptorSets[currentFrame], 1, &firstBlock);
    } else {
        // (The gizmo shader keeps a plain uniform buffer.)
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_descriptorSets[currentFrame], 0, nullptr);
    }
}

void Shader::updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo) {
    static int callCount = 0;
    callCount++;
    
    // Log first few updates to verify data
    if (callCount <= 3) {
        CrashHandler::logInfo("=== Shader::updateUniformBuffer (call #" + std::to_string(callCount) + ") ===");
        CrashHandler::logInfo("Frame: " + std::to_string(currentFrame));
        CrashHandler::logInfo("UBO colorAndExposure: (" + 
            std::to_string(ubo.colorAndExposure.r) + ", " +
            std::to_string(ubo.colorAndExposure.g) + ", " +
            std::to_string(ubo.colorAndExposure.b) + ", " +
            std::to_string(ubo.colorAndExposure.a) + ")");
        CrashHandler::logInfo("UBO size: " + std::to_string(sizeof(ubo)) + " bytes");
        CrashHandler::logInfo("Mapped buffer address: " + std::to_string(reinterpret_cast<uintptr_t>(m_uniformBuffersMapped[currentFrame])));
    }
    
    memcpy(m_uniformBuffersMapped[currentFrame], &ubo, sizeof(ubo));
    
    // Verify the data was written
    if (callCount <= 3) {
        const UniformBufferObject* mappedUBO = static_cast<const UniformBufferObject*>(m_uniformBuffersMapped[currentFrame]);
        CrashHandler::logInfo("After memcpy - colorAndExposure: (" + 
            std::to_string(mappedUBO->colorAndExposure.r) + ", " +
            std::to_string(mappedUBO->colorAndExposure.g) + ", " +
            std::to_string(mappedUBO->colorAndExposure.b) + ", " +
            std::to_string(mappedUBO->colorAndExposure.a) + ")");
    }
}

void Shader::updateGizmoUniformBuffer(uint32_t currentFrame, const GizmoUniformBufferObject& ubo) {
    memcpy(m_uniformBuffersMapped[currentFrame], &ubo, sizeof(ubo));
}

void Shader::pushConstants(VkCommandBuffer commandBuffer, const GizmoPushConstants& constants) {
    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GizmoPushConstants), &constants);
}

void Shader::createGizmoDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(m_context->getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo descriptor set layout!");
    }
}

void Shader::createGizmoPipeline(const std::string& vertexPath, const std::string& fragmentPath, VkRenderPass renderPass) {
    CrashHandler::logInfo("Creating gizmo pipeline with push constants");
    
    auto vertShaderCode = readFile(vertexPath);
    auto fragShaderCode = readFile(fragmentPath);

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

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

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  // Back-face culling to fix transparency artifacts
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;  // Disable sample shading - not needed for solid gizmos
    multisampling.minSampleShading = 1.0f;
    multisampling.rasterizationSamples = m_msaaSamples;
    multisampling.alphaToCoverageEnable = VK_FALSE;  // Disable - causes grainy appearance with transparency
    multisampling.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;    // Enable depth test for correct gizmo self-occlusion
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;  // Enable alpha blending for transparency
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Push constant range for model matrix and color
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(GizmoPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo pipeline layout!");
    }

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

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
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo graphics pipeline!");
    }

    vkDestroyShaderModule(m_context->getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(m_context->getDevice(), vertShaderModule, nullptr);
    
    CrashHandler::logInfo("Gizmo pipeline created successfully");
}

void Shader::createGizmoUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(GizmoUniformBufferObject);

    m_uniformBuffers.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_context->getDevice(), &bufferInfo, nullptr, &m_uniformBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create gizmo uniform buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_context->getDevice(), m_uniformBuffers[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = m_context->findMemoryType(memRequirements.memoryTypeBits, 
                                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &m_uniformBuffersMemory[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate gizmo uniform buffer memory!");
        }

        vkBindBufferMemory(m_context->getDevice(), m_uniformBuffers[i], m_uniformBuffersMemory[i], 0);
        vkMapMemory(m_context->getDevice(), m_uniformBuffersMemory[i], 0, bufferSize, 0, &m_uniformBuffersMapped[i]);
    }
}

void Shader::createGizmoDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(VulkanContext::MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_context->getDescriptorPool();
    allocInfo.descriptorSetCount = static_cast<uint32_t>(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate gizmo descriptor sets!");
    }

    for (size_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GizmoUniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_context->getDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

VkShaderModule Shader::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_context->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}

std::vector<char> Shader::readFile(const std::string& filename) {
    // Get absolute path for debugging
    char fullPath[MAX_PATH];
    GetFullPathNameA(filename.c_str(), MAX_PATH, fullPath, nullptr);
    CrashHandler::logInfo("Attempting to read shader from: " + std::string(fullPath));
    
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        CrashHandler::logError("Failed to open shader file: " + filename);
        throw std::runtime_error("Failed to open file: " + filename);
    }

    size_t fileSize = (size_t)file.tellg();
    CrashHandler::logInfo("Successfully opened shader, size: " + std::to_string(fileSize) + " bytes");
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
}

void Shader::updateUniformBufferDynamic(uint32_t currentFrame, const UniformBufferObject& ubo) {
    // Write UBO at the current object index within the dynamic buffer
    if (m_objectIndex >= MAX_OBJECTS) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            CrashHandler::logWarning("More than " + std::to_string(MAX_OBJECTS) + " objects in one frame; the extra ones reuse the last block");
        }
        m_objectIndex = MAX_OBJECTS - 1;
    }
    char* bufferData = static_cast<char*>(m_uniformBuffersMapped[currentFrame]);
    memcpy(bufferData + (static_cast<size_t>(m_objectIndex) * m_dynamicAlignment), &ubo, sizeof(UniformBufferObject));
    m_objectIndex++;
}

void Shader::bindWithDynamicOffset(VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t objectIndex) {
    // Calculate dynamic offset for this object (never past the last block in the buffer)
    if (objectIndex >= MAX_OBJECTS) objectIndex = MAX_OBJECTS - 1;
    uint32_t dynamicOffset = static_cast<uint32_t>(objectIndex * m_dynamicAlignment);
    
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout,
        0,
        1,
        &m_descriptorSets[currentFrame],
        1,
        &dynamicOffset
    );
}

void Shader::resetObjectIndex() {
    m_objectIndex = 0;
}

} // namespace Propulsion
