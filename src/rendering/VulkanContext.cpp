#include "rendering/VulkanContext.h"
#include "core/CrashHandler.h"
#include <imgui_impl_vulkan.h>
#define NOMINMAX
#include <array>
#include <fstream>
#include <iostream>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace Propulsion {

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

VulkanContext::VulkanContext(GLFWwindow* window) 
    : m_window(window), m_instance(VK_NULL_HANDLE), m_debugMessenger(VK_NULL_HANDLE),
      m_physicalDevice(VK_NULL_HANDLE), m_device(VK_NULL_HANDLE), m_surface(VK_NULL_HANDLE),
      m_swapChain(VK_NULL_HANDLE), m_renderPass(VK_NULL_HANDLE), m_commandPool(VK_NULL_HANDLE),
      m_descriptorPool(VK_NULL_HANDLE), m_depthImage(VK_NULL_HANDLE), m_depthImageMemory(VK_NULL_HANDLE),
      m_depthImageView(VK_NULL_HANDLE), m_offscreenRenderPass(VK_NULL_HANDLE), m_offscreenImage(VK_NULL_HANDLE),
      m_offscreenImageMemory(VK_NULL_HANDLE), m_offscreenImageView(VK_NULL_HANDLE),
      m_offscreenDepthImage(VK_NULL_HANDLE), m_offscreenDepthMemory(VK_NULL_HANDLE),
      m_offscreenDepthView(VK_NULL_HANDLE), m_offscreenFramebuffer(VK_NULL_HANDLE),
      m_offscreenSampler(VK_NULL_HANDLE), m_offscreenDescriptorSet(VK_NULL_HANDLE),
      m_selectionMaskRenderPass(VK_NULL_HANDLE), m_selectionMaskImage(VK_NULL_HANDLE),
      m_selectionMaskImageMemory(VK_NULL_HANDLE), m_selectionMaskImageView(VK_NULL_HANDLE),
      m_selectionMaskDepthImage(VK_NULL_HANDLE), m_selectionMaskDepthMemory(VK_NULL_HANDLE),
      m_selectionMaskDepthView(VK_NULL_HANDLE), m_selectionMaskFramebuffer(VK_NULL_HANDLE),
      m_selectionMaskSampler(VK_NULL_HANDLE), m_selectionMaskDescriptorSet(VK_NULL_HANDLE),
      m_dilationRenderPass(VK_NULL_HANDLE), m_tempMaskImage(VK_NULL_HANDLE),
      m_tempMaskImageMemory(VK_NULL_HANDLE), m_tempMaskImageView(VK_NULL_HANDLE),
      m_tempMaskFramebuffer(VK_NULL_HANDLE), m_dilatedMaskImage(VK_NULL_HANDLE),
      m_dilatedMaskImageMemory(VK_NULL_HANDLE), m_dilatedMaskImageView(VK_NULL_HANDLE),
      m_dilatedMaskFramebuffer(VK_NULL_HANDLE), m_tempMaskDescriptorSet(VK_NULL_HANDLE),
      m_dilatedMaskDescriptorSet(VK_NULL_HANDLE), m_selectionMaskSamplerDescriptor(VK_NULL_HANDLE),
      m_tempMaskSamplerDescriptor(VK_NULL_HANDLE), m_dilationDescriptorSetLayout(VK_NULL_HANDLE),
      m_dilationPipelineLayout(VK_NULL_HANDLE), m_dilateHPipeline(VK_NULL_HANDLE),
      m_dilateVPipeline(VK_NULL_HANDLE),
      m_compositeImage(VK_NULL_HANDLE), m_compositeImageMemory(VK_NULL_HANDLE),
      m_compositeImageView(VK_NULL_HANDLE), m_compositeFramebuffer(VK_NULL_HANDLE),
      m_compositeDescriptorSet(VK_NULL_HANDLE), m_compositeDescriptorSetLayout(VK_NULL_HANDLE),
      m_compositePipelineLayout(VK_NULL_HANDLE), m_compositePipeline(VK_NULL_HANDLE),
      m_compositeInputDescriptorSet(VK_NULL_HANDLE),
      m_gizmoMsaaRenderPass(VK_NULL_HANDLE), m_gizmoMsaaColorImage(VK_NULL_HANDLE),
      m_gizmoMsaaColorMemory(VK_NULL_HANDLE), m_gizmoMsaaColorView(VK_NULL_HANDLE),
      m_gizmoMsaaDepthImage(VK_NULL_HANDLE), m_gizmoMsaaDepthMemory(VK_NULL_HANDLE),
      m_gizmoMsaaDepthView(VK_NULL_HANDLE), m_gizmoResolveImage(VK_NULL_HANDLE),
      m_gizmoResolveMemory(VK_NULL_HANDLE), m_gizmoResolveView(VK_NULL_HANDLE),
      m_gizmoMsaaFramebuffer(VK_NULL_HANDLE), m_gizmoBlendRenderPass(VK_NULL_HANDLE),
      m_gizmoBlendFramebuffer(VK_NULL_HANDLE), m_gizmoBlendPipeline(VK_NULL_HANDLE),
      m_gizmoBlendPipelineLayout(VK_NULL_HANDLE), m_gizmoBlendDescriptorSetLayout(VK_NULL_HANDLE),
      m_gizmoBlendDescriptorSet(VK_NULL_HANDLE), m_gizmoResolveSampler(VK_NULL_HANDLE),
      m_assetPreviewImage(VK_NULL_HANDLE), m_assetPreviewImageMemory(VK_NULL_HANDLE),
      m_assetPreviewImageView(VK_NULL_HANDLE), m_assetPreviewDepthImage(VK_NULL_HANDLE),
      m_assetPreviewDepthMemory(VK_NULL_HANDLE), m_assetPreviewDepthView(VK_NULL_HANDLE),
      m_assetPreviewFramebuffer(VK_NULL_HANDLE), m_assetPreviewSampler(VK_NULL_HANDLE),
      m_assetPreviewDescriptorSet(VK_NULL_HANDLE),
      m_currentFrame(0), m_imageIndex(0), m_framebufferResized(false) {
    
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createDepthResources();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    createDescriptorPool();
    createOffscreenResources();  // Create offscreen render target
    createSelectionMaskResources();  // Create selection mask render target
    createDilationResources();  // Create dilation pass render targets
    createCompositeResources();  // Create composite pass resources
    createAssetPreviewResources();  // Create asset preview render target
}

VulkanContext::~VulkanContext() {
    waitIdle();
    
    cleanupAssetPreviewResources();
    cleanupCompositeResources();
    cleanupDilationResources();
    cleanupSelectionMaskResources();
    cleanupOffscreenResources();
    cleanupSwapChain();
    
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    
    // Destroy per-frame semaphores and fences
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }
    
    // Destroy per-image semaphores
    for (size_t i = 0; i < m_renderFinishedSemaphores.size(); i++) {
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
    }
    
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    vkDestroyDevice(m_device, nullptr);
    
    if (m_enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }
    
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
}

void VulkanContext::createInstance() {
    if (m_enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "PropulsionMKI";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "PropulsionMKI";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance!");
    }

    CrashHandler::logInfo("Vulkan instance created");
}

void VulkanContext::setupDebugMessenger() {
    if (!m_enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("Failed to set up debug messenger!");
    }
}

void VulkanContext::createSurface() {
    if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }
    CrashHandler::logInfo("Vulkan surface created");
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // PROPULSION_GPU picks by number (as logged below) or by part of the name; otherwise the best suitable one is used.
    const char* wanted = std::getenv("PROPULSION_GPU");
    const std::string request = wanted ? wanted : "";

    GpuCandidate best;
    int bestIndex = -1;
    for (size_t i = 0; i < devices.size(); i++) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        const GpuCandidate candidate = describeGpu(devices[i]);
        CrashHandler::logInfo("GPU " + std::to_string(i) + ": " + properties.deviceName + (candidate.suitable ? "" : " (not usable)") +
                              (candidate.canQuery ? " (ray queries)" : ""));

        bool asked = false;
        if (!request.empty()) {
            asked = request == std::to_string(i) || std::string(properties.deviceName).find(request) != std::string::npos;
        }
        if (!candidate.suitable) continue;
        if (!request.empty()) {
            if (asked) { bestIndex = static_cast<int>(i); break; }
            continue;
        }
        if (bestIndex < 0 || gpuIsBetter(candidate, best)) {
            best = candidate;
            bestIndex = static_cast<int>(i);
        }
    }
    if (bestIndex < 0 && !request.empty()) {
        CrashHandler::logWarning("PROPULSION_GPU=\"" + request + "\" matches no usable GPU; choosing automatically");
        for (size_t i = 0; i < devices.size(); i++) {
            const GpuCandidate candidate = describeGpu(devices[i]);
            if (!candidate.suitable) continue;
            if (bestIndex < 0 || gpuIsBetter(candidate, best)) {
                best = candidate;
                bestIndex = static_cast<int>(i);
            }
        }
    }

    if (bestIndex < 0) {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
    m_physicalDevice = devices[bestIndex];

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    m_gpuName = properties.deviceName;
    CrashHandler::logInfo("Selected GPU: " + m_gpuName);
}

GpuCandidate VulkanContext::describeGpu(VkPhysicalDevice device) {
    GpuCandidate candidate;
    candidate.suitable = isDeviceSuitable(device);

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    candidate.discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    candidate.canQuery = queryRayTracingCaps(device).canQuery();

    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(device, &memory);
    for (uint32_t i = 0; i < memory.memoryHeapCount; i++) {
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            candidate.vramBytes = (std::max)(candidate.vramBytes, static_cast<uint64_t>(memory.memoryHeaps[i].size));
        }
    }
    return candidate;
}

RayTracingCaps VulkanContext::queryRayTracingCaps(VkPhysicalDevice device) const {
    RayTracingCaps caps;
    const char* disabled = std::getenv("PROPULSION_DISABLE_RT");
    if (disabled && disabled[0] != '\0' && disabled[0] != '0') return caps;

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_2) return caps;

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    auto has = [&](const char* name) {
        for (const auto& extension : extensions) {
            if (std::strcmp(extension.extensionName, name) == 0) return true;
        }
        return false;
    };

    const bool hasStructure = has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    const bool hasQuery = has(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    const bool hasPipeline = has(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    caps.deferredHostOperations = has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

    // Only structs for extensions the device has may be chained into the query.
    VkPhysicalDeviceVulkan12Features v12{};
    v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR structure{};
    structure.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceRayQueryFeaturesKHR query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR pipeline{};
    pipeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

    void* chain = &v12;
    if (hasStructure) { structure.pNext = chain; chain = &structure; }
    if (hasQuery) { query.pNext = chain; chain = &query; }
    if (hasPipeline) { pipeline.pNext = chain; chain = &pipeline; }

    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = chain;
    vkGetPhysicalDeviceFeatures2(device, &features);

    caps.accelerationStructure = hasStructure && structure.accelerationStructure;
    caps.rayQuery = hasQuery && query.rayQuery;
    caps.rayTracingPipeline = hasPipeline && pipeline.rayTracingPipeline;
    caps.bufferDeviceAddress = v12.bufferDeviceAddress;
    caps.scalarBlockLayout = v12.scalarBlockLayout;
    caps.shaderInt64 = features.features.shaderInt64;
    caps.descriptorIndexing = v12.runtimeDescriptorArray && v12.shaderSampledImageArrayNonUniformIndexing && v12.descriptorBindingPartiallyBound;
    return caps;
}

bool VulkanContext::supportsBindlessTextures(VkPhysicalDevice device) const {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_2) return false;

    VkPhysicalDeviceVulkan12Features v12{};
    v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &v12;
    vkGetPhysicalDeviceFeatures2(device, &features);
    return v12.runtimeDescriptorArray && v12.shaderSampledImageArrayNonUniformIndexing && v12.descriptorBindingPartiallyBound;
}

void VulkanContext::createLogicalDevice() {
    m_queueFamilies = findQueueFamilies(m_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {m_queueFamilies.graphicsFamily.value(), m_queueFamilies.presentFamily.value()};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.sampleRateShading = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;

    // Ray tracing: switched on only when the GPU can do ray queries at least (see RayTracingSupport.h).
    const RayTracingCaps supported = queryRayTracingCaps(m_physicalDevice);
    std::vector<const char*> extensions = m_deviceExtensions;

    VkPhysicalDeviceVulkan12Features v12{};
    v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR structureFeatures{};
    structureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceRayQueryFeaturesKHR queryFeatures{};
    queryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR pipelineFeatures{};
    pipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

    // Every material's textures are in one big array that the scene shader indexes (see TextureTable.h)
    v12.runtimeDescriptorArray = VK_TRUE;
    v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    v12.descriptorBindingPartiallyBound = VK_TRUE;

    void* featureChain = &v12;
    if (supported.canQuery()) {
        m_rtCaps = supported;
        m_rtCaps.rayTracingPipeline = supported.canTracePipeline();  // the pipeline needs its deferred-operations partner

        deviceFeatures.shaderInt64 = VK_TRUE;
        v12.bufferDeviceAddress = VK_TRUE;
        v12.scalarBlockLayout = VK_TRUE;
        structureFeatures.accelerationStructure = VK_TRUE;
        queryFeatures.rayQuery = VK_TRUE;
        extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

        v12.pNext = &structureFeatures;
        structureFeatures.pNext = &queryFeatures;
        if (m_rtCaps.rayTracingPipeline) {
            pipelineFeatures.rayTracingPipeline = VK_TRUE;
            extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            queryFeatures.pNext = &pipelineFeatures;
        }
    } else {
        m_rtCaps = RayTracingCaps{};
    }

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.features = deviceFeatures;
    features2.pNext = featureChain;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = nullptr;  // the features go in features2
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (m_enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device!");
    }

    vkGetDeviceQueue(m_device, m_queueFamilies.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueFamilies.presentFamily.value(), 0, &m_presentQueue);

    if (m_rtCaps.canQuery()) {
        auto load = [&](auto& pointer, const char* name) {
            pointer = reinterpret_cast<std::remove_reference_t<decltype(pointer)>>(vkGetDeviceProcAddr(m_device, name));
            return pointer != nullptr;
        };
        const bool loaded = load(m_rtFns.vkCreateAccelerationStructureKHR, "vkCreateAccelerationStructureKHR") &&
                            load(m_rtFns.vkDestroyAccelerationStructureKHR, "vkDestroyAccelerationStructureKHR") &&
                            load(m_rtFns.vkGetAccelerationStructureBuildSizesKHR, "vkGetAccelerationStructureBuildSizesKHR") &&
                            load(m_rtFns.vkCmdBuildAccelerationStructuresKHR, "vkCmdBuildAccelerationStructuresKHR") &&
                            load(m_rtFns.vkGetAccelerationStructureDeviceAddressKHR, "vkGetAccelerationStructureDeviceAddressKHR");
        if (!loaded) {
            CrashHandler::logWarning("Ray tracing entry points are missing; ray tracing is off");
            m_rtCaps = RayTracingCaps{};
            m_rtFns = RayTracingFunctions{};
        }

        VkPhysicalDeviceAccelerationStructurePropertiesKHR structureProperties{};
        structureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &structureProperties;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties2);
        if (structureProperties.minAccelerationStructureScratchOffsetAlignment > 0) {
            m_asScratchAlignment = structureProperties.minAccelerationStructureScratchOffsetAlignment;
        }
    }

    CrashHandler::logInfo("Vulkan logical device created");
    CrashHandler::logInfo(std::string("Ray tracing: ") +
                          (m_rtCaps.canTracePipeline() ? "ray queries and ray tracing pipelines" :
                           m_rtCaps.canQuery() ? "ray queries only" : "not available"));
}

void VulkanContext::createSwapChain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = {m_queueFamilies.graphicsFamily.value(), m_queueFamilies.presentFamily.value()};

    if (m_queueFamilies.graphicsFamily != m_queueFamilies.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;
    
    CrashHandler::logInfo("Swap chain created");
}

void VulkanContext::createImageViews() {
    m_swapChainImageViews.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image views!");
        }
    }
}

void VulkanContext::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
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

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass!");
    }
    
    CrashHandler::logInfo("Render pass created");
}

void VulkanContext::createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_swapChainExtent.width;
    imageInfo.extent.height = m_swapChainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_depthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_depthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_depthImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate depth image memory!");
    }

    vkBindImageMemory(m_device, m_depthImage, m_depthImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view!");
    }
}

void VulkanContext::createFramebuffers() {
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());

    for (size_t i = 0; i < m_swapChainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            m_swapChainImageViews[i],
            m_depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }
}

void VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilies.graphicsFamily.value();

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool!");
    }
}

void VulkanContext::createCommandBuffers() {
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers!");
    }
}

void VulkanContext::createSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(m_swapChainImages.size());  // One per swapchain image
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // Create image available semaphores (one per frame in flight)
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects!");
        }
    }
    
    // Create render finished semaphores (one per swapchain image)
    for (size_t i = 0; i < m_swapChainImages.size(); i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create render finished semaphore!");
        }
    }
}

void VulkanContext::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 11> poolSizes = {
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * poolSizes.size();
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool!");
    }
}

bool VulkanContext::waitForFrame() {
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX, 
                                            m_imageAvailableSemaphores[m_currentFrame], 
                                            VK_NULL_HANDLE, &m_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        recreateSwapChain(width, height);
        return false;  // Caller should skip this frame
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
    return true;
}

void VulkanContext::beginFrame() {
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }
    
    // NOTE: We no longer automatically start the main render pass here
    // The Engine will manage render passes (offscreen first, then main)
}

void VulkanContext::beginMainRenderPass() {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapChainFramebuffers[m_imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapChainExtent;

    // Clear to dark gray
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}}; // Dark gray
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanContext::endFrame() {
    vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);

    VkResult endResult = vkEndCommandBuffer(m_commandBuffers[m_currentFrame]);
    if (endResult != VK_SUCCESS) {
        std::string errorMsg = "Failed to record command buffer! VkResult: " + std::to_string(endResult);
        CrashHandler::logError(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_imageIndex]};  // Use image index
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitResult = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
    if (submitResult != VK_SUCCESS) {
        std::string errorMsg = "Failed to submit draw command buffer! VkResult: " + std::to_string(submitResult);
        CrashHandler::logError(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {m_swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_imageIndex;

    VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        recreateSwapChain(width, height);
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swap chain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanContext::waitIdle() {
    vkDeviceWaitIdle(m_device);
}

void VulkanContext::recreateSwapChain(int width, int height) {
    if (width == 0 || height == 0) return;

    vkDeviceWaitIdle(m_device);

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createDepthResources();
    createFramebuffers();
    
    // Recreate render finished semaphores for the new swapchain image count
    for (auto semaphore : m_renderFinishedSemaphores) {
        vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    m_renderFinishedSemaphores.clear();
    m_renderFinishedSemaphores.resize(m_swapChainImages.size());
    
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (size_t i = 0; i < m_swapChainImages.size(); i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to recreate render finished semaphore!");
        }
    }
}

void VulkanContext::cleanupSwapChain() {
    vkDestroyImageView(m_device, m_depthImageView, nullptr);
    vkDestroyImage(m_device, m_depthImage, nullptr);
    vkFreeMemory(m_device, m_depthImageMemory, nullptr);

    for (auto framebuffer : m_swapChainFramebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }

    for (auto imageView : m_swapChainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

uint32_t VulkanContext::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

void VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
                                 VkDeviceMemory& memory, bool deviceAddress) {
    if (deviceAddress) usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &requirements);

    VkMemoryAllocateFlagsInfo flags{};
    flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = deviceAddress ? &flags : nullptr;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to allocate buffer memory!");
    }
    vkBindBufferMemory(m_device, buffer, memory, 0);
}

VkDeviceAddress VulkanContext::getBufferAddress(VkBuffer buffer) const {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(m_device, &info);
}

VkFormat VulkanContext::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported format!");
}

VkFormat VulkanContext::findDepthFormat() {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

bool VulkanContext::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : m_validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

std::vector<const char*> VulkanContext::getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (m_enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy &&
           supportsBindlessTextures(device);
}

QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }

        i++;
    }

    return indices;
}

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

SwapChainSupportDetails VulkanContext::querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanContext::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    CrashHandler::logInfo("=== Available Surface Formats ===");
    for (size_t i = 0; i < availableFormats.size(); i++) {
        CrashHandler::logInfo("Format " + std::to_string(i) + ": format=" + std::to_string(availableFormats[i].format) + 
                             " colorSpace=" + std::to_string(availableFormats[i].colorSpace));
    }
    
    // Prefer SRGB for auto-gamma correction
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            CrashHandler::logInfo("Selected: VK_FORMAT_B8G8R8A8_UNORM with SRGB_NONLINEAR");
            return availableFormat;
        }
    }
    
    // Fallback to UNORM is SRGB is not available
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_R8G8B8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            CrashHandler::logInfo("Selected: VK_FORMAT_R8G8B8A8_UNORM with SRGB_NONLINEAR");
            return availableFormat;
        }
    }

    CrashHandler::logInfo("Selected: Fallback format");
    return availableFormats[0];
}

VkPresentModeKHR VulkanContext::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    // Use FIFO (V-Sync) for synchronized presentation
    // FIFO is guaranteed to be available and prevents tearing
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanContext::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)()) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        // The driver can call this from any thread; the log is safe to write to from all of them.
        const bool aboutOurCalls = (messageType & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) != 0;
        if (aboutOurCalls) {
            const LogLevel level = messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? LogLevel::Error : LogLevel::Warning;
            OutputLog::write(LogCategory::Vulkan, level, std::string("Validation layer: ") + pCallbackData->pMessage);
        } else {
            // A general message is the Vulkan loader talking about the machine, not about what the engine did: a layer that
            // another program (ReShade, OBS, a game's VR mod) registered and left broken, a layer found twice, and so on.
            // They are true but not ours to fix, so they are Verbose: the panel's Verbose filter shows them.
            OutputLog::write(LogCategory::Vulkan, LogLevel::Verbose, std::string("Vulkan loader: ") + pCallbackData->pMessage);
        }
    }

    return VK_FALSE;
}

void VulkanContext::createOffscreenResources() {
    CrashHandler::logInfo("Creating offscreen render target...");
    
    // Use a fixed size for now (can be made dynamic later)
    m_offscreenExtent = {2048, 2048};
    
    createOffscreenRenderPass();
    createOffscreenFramebuffer();
    // NOTE: Descriptor set will be created later via initializeOffscreenDescriptor() after ImGui is initialized
    
    CrashHandler::logInfo("Offscreen render target created: " + std::to_string(m_offscreenExtent.width) + "x" + std::to_string(m_offscreenExtent.height));
}

void VulkanContext::createOffscreenRenderPass() {
    // Color attachment
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;  // Use consistent format
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // Will be sampled by ImGui
    
    // Depth attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
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
    
    // Subpass dependency for layout transitions
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
    
    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_offscreenRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen render pass!");
    }
}

void VulkanContext::createOffscreenFramebuffer() {
    // Create color image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_offscreenExtent.width;
    imageInfo.extent.height = m_offscreenExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // STORAGE: the ray tracing debug view writes the viewport image from a compute shader (see RayDebugPass.h)
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_offscreenImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen image!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_offscreenImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_offscreenImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate offscreen image memory!");
    }
    
    vkBindImageMemory(m_device, m_offscreenImage, m_offscreenImageMemory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_offscreenImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_offscreenImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen image view!");
    }
    
    // Create depth image
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent.width = m_offscreenExtent.width;
    depthImageInfo.extent.height = m_offscreenExtent.height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = findDepthFormat();
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(m_device, &depthImageInfo, nullptr, &m_offscreenDepthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen depth image!");
    }
    
    vkGetImageMemoryRequirements(m_device, m_offscreenDepthImage, &memRequirements);
    
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_offscreenDepthMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate offscreen depth memory!");
    }
    
    vkBindImageMemory(m_device, m_offscreenDepthImage, m_offscreenDepthMemory, 0);
    
    // Create depth image view
    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_offscreenDepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = findDepthFormat();
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &depthViewInfo, nullptr, &m_offscreenDepthView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen depth image view!");
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
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_offscreenSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen sampler!");
    }
    
    // Create framebuffer
    std::array<VkImageView, 2> attachments = {m_offscreenImageView, m_offscreenDepthView};
    
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_offscreenRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = m_offscreenExtent.width;
    framebufferInfo.height = m_offscreenExtent.height;
    framebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_offscreenFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create offscreen framebuffer!");
    }
}

void VulkanContext::initializeOffscreenDescriptor() {
    // Use ImGui's helper function to create a descriptor set for our offscreen texture
    m_offscreenDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_offscreenSampler,
        m_offscreenImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    
    if (m_offscreenDescriptorSet == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create offscreen descriptor set!");
    }
    
    CrashHandler::logInfo("Offscreen descriptor set initialized");
}

void VulkanContext::cleanupOffscreenResources() {
    if (m_device == VK_NULL_HANDLE) return;
    
    vkDestroyFramebuffer(m_device, m_offscreenFramebuffer, nullptr);
    vkDestroySampler(m_device, m_offscreenSampler, nullptr);
    vkDestroyImageView(m_device, m_offscreenDepthView, nullptr);
    vkDestroyImage(m_device, m_offscreenDepthImage, nullptr);
    vkFreeMemory(m_device, m_offscreenDepthMemory, nullptr);
    vkDestroyImageView(m_device, m_offscreenImageView, nullptr);
    vkDestroyImage(m_device, m_offscreenImage, nullptr);
    vkFreeMemory(m_device, m_offscreenImageMemory, nullptr);
    vkDestroyRenderPass(m_device, m_offscreenRenderPass, nullptr);
}

void VulkanContext::createSelectionMaskResources() {
    CrashHandler::logInfo("Creating selection mask render target...");
    
    // Use same size as offscreen rendering for now (will be updated on viewport resize)
    m_selectionMaskExtent = {2048, 2048};
    
    createSelectionMaskRenderPass();
    createSelectionMaskFramebuffer();
    // NOTE: Descriptor set will be created later via initializeSelectionMaskDescriptor() after ImGui is initialized
    
    CrashHandler::logInfo("Selection mask render target created: " + std::to_string(m_selectionMaskExtent.width) + "x" + std::to_string(m_selectionMaskExtent.height));
}

void VulkanContext::createSelectionMaskRenderPass() {
    // Color attachment - RGBA for mask (displays as grayscale in ImGui)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Depth attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
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
    
    // Subpass dependency for layout transitions
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
    
    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_selectionMaskRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create selection mask render pass!");
    }
}

void VulkanContext::createSelectionMaskFramebuffer() {
    // Create color image (RGBA for proper display in ImGui)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_selectionMaskExtent.width;
    imageInfo.extent.height = m_selectionMaskExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_selectionMaskImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create selection mask image!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_selectionMaskImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_selectionMaskImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate selection mask image memory!");
    }
    
    vkBindImageMemory(m_device, m_selectionMaskImage, m_selectionMaskImageMemory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_selectionMaskImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_selectionMaskImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create selection mask image view!");
    }
    
    // Create depth image (reuse same depth format as main rendering)
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent.width = m_selectionMaskExtent.width;
    depthImageInfo.extent.height = m_selectionMaskExtent.height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = findDepthFormat();
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(m_device, &depthImageInfo, nullptr, &m_selectionMaskDepthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create selection mask depth image!");
    }
    
    vkGetImageMemoryRequirements(m_device, m_selectionMaskDepthImage, &memRequirements);
    
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_selectionMaskDepthMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate selection mask depth memory!");
    }
    
    vkBindImageMemory(m_device, m_selectionMaskDepthImage, m_selectionMaskDepthMemory, 0);
    
    // Create depth image view
    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_selectionMaskDepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = findDepthFormat();
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &depthViewInfo, nullptr, &m_selectionMaskDepthView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create selection mask depth image view!");
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
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_selectionMaskSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create selection mask sampler!");
    }
    
    // Create framebuffer
    std::array<VkImageView, 2> attachments = {m_selectionMaskImageView, m_selectionMaskDepthView};
    
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_selectionMaskRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = m_selectionMaskExtent.width;
    framebufferInfo.height = m_selectionMaskExtent.height;
    framebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_selectionMaskFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create selection mask framebuffer!");
    }
}

void VulkanContext::initializeSelectionMaskDescriptor() {
    // Use ImGui's helper function to create a descriptor set for our selection mask texture
    m_selectionMaskDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_selectionMaskSampler,
        m_selectionMaskImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    
    if (m_selectionMaskDescriptorSet == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create selection mask descriptor set!");
    }
    
    CrashHandler::logInfo("Selection mask descriptor set initialized");
}

void VulkanContext::cleanupSelectionMaskResources() {
    if (m_device == VK_NULL_HANDLE) return;
    
    vkDestroyFramebuffer(m_device, m_selectionMaskFramebuffer, nullptr);
    vkDestroySampler(m_device, m_selectionMaskSampler, nullptr);
    vkDestroyImageView(m_device, m_selectionMaskDepthView, nullptr);
    vkDestroyImage(m_device, m_selectionMaskDepthImage, nullptr);
    vkFreeMemory(m_device, m_selectionMaskDepthMemory, nullptr);
    vkDestroyImageView(m_device, m_selectionMaskImageView, nullptr);
    vkDestroyImage(m_device, m_selectionMaskImage, nullptr);
    vkFreeMemory(m_device, m_selectionMaskImageMemory, nullptr);
    vkDestroyRenderPass(m_device, m_selectionMaskRenderPass, nullptr);
}

// ===== Dilation Resources Implementation =====

void VulkanContext::createDilationResources() {
    CrashHandler::logInfo("Creating dilation pass render targets...");
    
    createDilationRenderPass();
    createDilationFramebuffers();
    createDilationPipelines();
    // NOTE: Descriptor sets will be created later via initializeDilationDescriptors() after ImGui is initialized
    
    CrashHandler::logInfo("Dilation pass render targets created: " + std::to_string(m_selectionMaskExtent.width) + "x" + std::to_string(m_selectionMaskExtent.height));
}

void VulkanContext::createDilationRenderPass() {
    // Single color attachment - no depth needed for fullscreen post-process
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = nullptr;  // No depth
    
    // Dependencies for layout transitions
    std::array<VkSubpassDependency, 2> dependencies{};
    
    // External -> Subpass (wait for previous pass to finish writing)
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    // Subpass -> External (make sure writes complete before reading in next pass)
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    
    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_dilationRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create dilation render pass!");
    }
}

void VulkanContext::createDilationFramebuffers() {
    // Create TempMask image (for horizontal pass output)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_selectionMaskExtent.width;
    imageInfo.extent.height = m_selectionMaskExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_tempMaskImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create temp mask image!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_tempMaskImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_tempMaskImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate temp mask image memory!");
    }
    
    vkBindImageMemory(m_device, m_tempMaskImage, m_tempMaskImageMemory, 0);
    
    // Create TempMask image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_tempMaskImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_tempMaskImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create temp mask image view!");
    }
    
    // Create TempMask framebuffer
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_dilationRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &m_tempMaskImageView;
    framebufferInfo.width = m_selectionMaskExtent.width;
    framebufferInfo.height = m_selectionMaskExtent.height;
    framebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_tempMaskFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create temp mask framebuffer!");
    }
    
    // Create DilatedMask image (for vertical pass output / final dilated mask)
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_dilatedMaskImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create dilated mask image!");
    }
    
    vkGetImageMemoryRequirements(m_device, m_dilatedMaskImage, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_dilatedMaskImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate dilated mask image memory!");
    }
    
    vkBindImageMemory(m_device, m_dilatedMaskImage, m_dilatedMaskImageMemory, 0);
    
    // Create DilatedMask image view
    viewInfo.image = m_dilatedMaskImage;
    
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_dilatedMaskImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create dilated mask image view!");
    }
    
    // Create DilatedMask framebuffer
    framebufferInfo.pAttachments = &m_dilatedMaskImageView;
    
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_dilatedMaskFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create dilated mask framebuffer!");
    }
}

void VulkanContext::initializeDilationDescriptors() {
    // Create ImGui descriptor sets for viewing temp and dilated masks in debug UI
    m_tempMaskDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_selectionMaskSampler,  // Reuse the same sampler
        m_tempMaskImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    
    if (m_tempMaskDescriptorSet == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create temp mask descriptor set!");
    }
    
    m_dilatedMaskDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_selectionMaskSampler,
        m_dilatedMaskImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    
    if (m_dilatedMaskDescriptorSet == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create dilated mask descriptor set!");
    }
    
    // Create sampler descriptors for use in dilation shaders
    // These are descriptor sets that can be bound during the dilation passes
    m_selectionMaskSamplerDescriptor = ImGui_ImplVulkan_AddTexture(
        m_selectionMaskSampler,
        m_selectionMaskImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    
    m_tempMaskSamplerDescriptor = ImGui_ImplVulkan_AddTexture(
        m_selectionMaskSampler,
        m_tempMaskImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    
    // Create composite ImGui descriptor for viewing
    m_compositeDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_selectionMaskSampler,
        m_compositeImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    
    if (m_compositeDescriptorSet == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create composite descriptor set!");
    }
    
    // Create composite input descriptor set (needs scene, selection mask, and dilated mask)
    VkDescriptorSetAllocateInfo compositeAllocInfo{};
    compositeAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    compositeAllocInfo.descriptorPool = m_descriptorPool;
    compositeAllocInfo.descriptorSetCount = 1;
    compositeAllocInfo.pSetLayouts = &m_compositeDescriptorSetLayout;
    
    if (vkAllocateDescriptorSets(m_device, &compositeAllocInfo, &m_compositeInputDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate composite input descriptor set!");
    }
    
    // Update the composite descriptor set with three textures
    std::array<VkDescriptorImageInfo, 3> compositeImageInfos{};
    
    // Binding 0: Scene color
    compositeImageInfos[0].sampler = m_offscreenSampler;
    compositeImageInfos[0].imageView = m_offscreenImageView;
    compositeImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Binding 1: Selection mask (original)
    compositeImageInfos[1].sampler = m_selectionMaskSampler;
    compositeImageInfos[1].imageView = m_selectionMaskImageView;
    compositeImageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Binding 2: Dilated mask
    compositeImageInfos[2].sampler = m_selectionMaskSampler;
    compositeImageInfos[2].imageView = m_dilatedMaskImageView;
    compositeImageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    std::array<VkWriteDescriptorSet, 3> compositeDescriptorWrites{};
    
    compositeDescriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    compositeDescriptorWrites[0].dstSet = m_compositeInputDescriptorSet;
    compositeDescriptorWrites[0].dstBinding = 0;
    compositeDescriptorWrites[0].dstArrayElement = 0;
    compositeDescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compositeDescriptorWrites[0].descriptorCount = 1;
    compositeDescriptorWrites[0].pImageInfo = &compositeImageInfos[0];
    
    compositeDescriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    compositeDescriptorWrites[1].dstSet = m_compositeInputDescriptorSet;
    compositeDescriptorWrites[1].dstBinding = 1;
    compositeDescriptorWrites[1].dstArrayElement = 0;
    compositeDescriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compositeDescriptorWrites[1].descriptorCount = 1;
    compositeDescriptorWrites[1].pImageInfo = &compositeImageInfos[1];
    
    compositeDescriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    compositeDescriptorWrites[2].dstSet = m_compositeInputDescriptorSet;
    compositeDescriptorWrites[2].dstBinding = 2;
    compositeDescriptorWrites[2].dstArrayElement = 0;
    compositeDescriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compositeDescriptorWrites[2].descriptorCount = 1;
    compositeDescriptorWrites[2].pImageInfo = &compositeImageInfos[2];
    
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(compositeDescriptorWrites.size()), compositeDescriptorWrites.data(), 0, nullptr);
    
    CrashHandler::logInfo("Dilation and composite descriptor sets initialized");
}

void VulkanContext::createDilationPipelines() {
    // Helper to read shader file
    auto readFile = [](const std::string& filename) -> std::vector<char> {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + filename);
        }
        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    };
    
    // Create shader modules
    auto createShaderModule = [this](const std::vector<char>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        
        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module!");
        }
        return shaderModule;
    };
    
    // Load shaders
    auto fullscreenVert = readFile("shaders/fullscreen_vert.spv");
    auto dilateHFrag = readFile("shaders/dilate_h_frag.spv");
    auto dilateVFrag = readFile("shaders/dilate_v_frag.spv");
    
    VkShaderModule fullscreenVertModule = createShaderModule(fullscreenVert);
    VkShaderModule dilateHFragModule = createShaderModule(dilateHFrag);
    VkShaderModule dilateVFragModule = createShaderModule(dilateVFrag);
    
    // Descriptor set layout: one combined image sampler
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_dilationDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create dilation descriptor set layout!");
    }
    
    // Push constant range for texelSize and radii
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 2 + sizeof(int) * 2;  // vec2 texelSize + int radiusX + int radiusY
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_dilationDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_dilationPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create dilation pipeline layout!");
    }
    
    // Common pipeline settings
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = fullscreenVertModule;
    vertShaderStageInfo.pName = "main";
    
    // No vertex input (fullscreen triangle from gl_VertexIndex)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Dynamic viewport and scissor
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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Create horizontal dilation pipeline
    VkPipelineShaderStageCreateInfo dilateHFragStageInfo{};
    dilateHFragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    dilateHFragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    dilateHFragStageInfo.module = dilateHFragModule;
    dilateHFragStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo hShaderStages[] = { vertShaderStageInfo, dilateHFragStageInfo };
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = hShaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr;  // No depth
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_dilationPipelineLayout;
    pipelineInfo.renderPass = m_dilationRenderPass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_dilateHPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create horizontal dilation pipeline!");
    }
    
    // Create vertical dilation pipeline
    VkPipelineShaderStageCreateInfo dilateVFragStageInfo{};
    dilateVFragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    dilateVFragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    dilateVFragStageInfo.module = dilateVFragModule;
    dilateVFragStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo vShaderStages[] = { vertShaderStageInfo, dilateVFragStageInfo };
    pipelineInfo.pStages = vShaderStages;
    
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_dilateVPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create vertical dilation pipeline!");
    }
    
    // Cleanup shader modules
    vkDestroyShaderModule(m_device, dilateVFragModule, nullptr);
    vkDestroyShaderModule(m_device, dilateHFragModule, nullptr);
    vkDestroyShaderModule(m_device, fullscreenVertModule, nullptr);
    
    CrashHandler::logInfo("Dilation pipelines created successfully");
}

void VulkanContext::executeDilationPasses(VkCommandBuffer commandBuffer, int radius, float viewportAspectRatio) {
    // Push constants structure (must match shader)
    // We need separate X and Y radii to account for aspect ratio
    struct DilationPushConstants {
        float texelSizeX;
        float texelSizeY;
        int radiusX;
        int radiusY;
    };
    
    // The mask texture is square (2048x2048), but the viewport might not be.
    // When the mask is stretched to fit a wide viewport:
    // - Horizontal direction gets stretched (pixels appear wider)
    // - To compensate, we need MORE dilation in the Y direction
    // 
    // If viewportAspectRatio > 1 (wider than tall):
    //   radiusY should be larger to compensate for vertical compression
    // If viewportAspectRatio < 1 (taller than wide):
    //   radiusX should be larger to compensate for horizontal compression
    
    int radiusX = radius;
    int radiusY = radius;
    
    if (viewportAspectRatio > 1.0f) {
        // Viewport is wider than tall - increase Y radius
        radiusY = static_cast<int>(radius * viewportAspectRatio + 0.5f);
    } else if (viewportAspectRatio < 1.0f) {
        // Viewport is taller than wide - increase X radius
        radiusX = static_cast<int>(radius / viewportAspectRatio + 0.5f);
    }
    
    DilationPushConstants pushConstants;
    pushConstants.texelSizeX = 1.0f / static_cast<float>(m_selectionMaskExtent.width);
    pushConstants.texelSizeY = 1.0f / static_cast<float>(m_selectionMaskExtent.height);
    pushConstants.radiusX = radiusX;
    pushConstants.radiusY = radiusY;
    
    VkExtent2D extent = m_selectionMaskExtent;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    
    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    
    // PASS 1: Horizontal dilation (SelectionMask -> TempMask)
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_dilationRenderPass;
    renderPassInfo.framebuffer = m_tempMaskFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_dilateHPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_dilationPipelineLayout, 0, 1, &m_selectionMaskSamplerDescriptor, 0, nullptr);
    vkCmdPushConstants(commandBuffer, m_dilationPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DilationPushConstants), &pushConstants);
    
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);  // Fullscreen triangle
    
    vkCmdEndRenderPass(commandBuffer);
    
    // PASS 2: Vertical dilation (TempMask -> DilatedMask)
    renderPassInfo.framebuffer = m_dilatedMaskFramebuffer;
    
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_dilateVPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_dilationPipelineLayout, 0, 1, &m_tempMaskSamplerDescriptor, 0, nullptr);
    vkCmdPushConstants(commandBuffer, m_dilationPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DilationPushConstants), &pushConstants);
    
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);  // Fullscreen triangle
    
    vkCmdEndRenderPass(commandBuffer);
}

void VulkanContext::cleanupDilationResources() {
    if (m_device == VK_NULL_HANDLE) return;
    
    vkDestroyPipeline(m_device, m_dilateVPipeline, nullptr);
    vkDestroyPipeline(m_device, m_dilateHPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_dilationPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_dilationDescriptorSetLayout, nullptr);
    
    vkDestroyFramebuffer(m_device, m_dilatedMaskFramebuffer, nullptr);
    vkDestroyImageView(m_device, m_dilatedMaskImageView, nullptr);
    vkDestroyImage(m_device, m_dilatedMaskImage, nullptr);
    vkFreeMemory(m_device, m_dilatedMaskImageMemory, nullptr);
    
    vkDestroyFramebuffer(m_device, m_tempMaskFramebuffer, nullptr);
    vkDestroyImageView(m_device, m_tempMaskImageView, nullptr);
    vkDestroyImage(m_device, m_tempMaskImage, nullptr);
    vkFreeMemory(m_device, m_tempMaskImageMemory, nullptr);
    
    vkDestroyRenderPass(m_device, m_dilationRenderPass, nullptr);
}

// ===== Composite Resources Implementation =====

void VulkanContext::createCompositeResources() {
    CrashHandler::logInfo("Creating composite pass resources...");
    
    createGizmoMsaaRenderPass();
    createGizmoBlendRenderPass();
    createCompositeFramebuffer();
    createCompositePipeline();
    createGizmoBlendPipeline();
    
    CrashHandler::logInfo("Composite pass resources created");
}

void VulkanContext::createGizmoMsaaRenderPass() {
    // MSAA render pass: renders gizmo with 4x MSAA, resolves to gizmo resolve image
    // Attachments: 0=MSAA color, 1=MSAA depth, 2=resolve target
    std::array<VkAttachmentDescription, 3> attachments{};
    
    // MSAA color attachment - clear to transparent
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = GIZMO_MSAA_SAMPLES;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // Don't need MSAA after resolve
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    // MSAA depth attachment
    attachments[1].format = findDepthFormat();
    attachments[1].samples = GIZMO_MSAA_SAMPLES;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    // Resolve target (single-sample gizmo image)
    attachments[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference resolveAttachmentRef{};
    resolveAttachmentRef.attachment = 2;
    resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    subpass.pResolveAttachments = &resolveAttachmentRef;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_gizmoMsaaRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo MSAA render pass!");
    }
}

void VulkanContext::createGizmoBlendRenderPass() {
    // Blend pass: samples resolved gizmo and blends onto composite
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Load existing composite
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_gizmoBlendRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo blend render pass!");
    }
}

void VulkanContext::createCompositeFramebuffer() {
    // Create composite image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_selectionMaskExtent.width;
    imageInfo.extent.height = m_selectionMaskExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_compositeImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create composite image!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_compositeImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_compositeImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate composite image memory!");
    }
    
    vkBindImageMemory(m_device, m_compositeImage, m_compositeImageMemory, 0);
    
    // Create composite image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_compositeImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_compositeImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create composite image view!");
    }
    
    // Create composite framebuffer (reuses dilation render pass)
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_dilationRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &m_compositeImageView;
    framebufferInfo.width = m_selectionMaskExtent.width;
    framebufferInfo.height = m_selectionMaskExtent.height;
    framebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_compositeFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create composite framebuffer!");
    }
    
    // ===== GIZMO MSAA RESOURCES =====
    
    // Create MSAA color image
    VkImageCreateInfo msaaColorInfo{};
    msaaColorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    msaaColorInfo.imageType = VK_IMAGE_TYPE_2D;
    msaaColorInfo.extent.width = m_selectionMaskExtent.width;
    msaaColorInfo.extent.height = m_selectionMaskExtent.height;
    msaaColorInfo.extent.depth = 1;
    msaaColorInfo.mipLevels = 1;
    msaaColorInfo.arrayLayers = 1;
    msaaColorInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    msaaColorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    msaaColorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    msaaColorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    msaaColorInfo.samples = GIZMO_MSAA_SAMPLES;
    msaaColorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(m_device, &msaaColorInfo, nullptr, &m_gizmoMsaaColorImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo MSAA color image!");
    }
    
    VkMemoryRequirements msaaColorMemReq;
    vkGetImageMemoryRequirements(m_device, m_gizmoMsaaColorImage, &msaaColorMemReq);
    
    VkMemoryAllocateInfo msaaColorAllocInfo{};
    msaaColorAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    msaaColorAllocInfo.allocationSize = msaaColorMemReq.size;
    msaaColorAllocInfo.memoryTypeIndex = findMemoryType(msaaColorMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &msaaColorAllocInfo, nullptr, &m_gizmoMsaaColorMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate gizmo MSAA color memory!");
    }
    vkBindImageMemory(m_device, m_gizmoMsaaColorImage, m_gizmoMsaaColorMemory, 0);
    
    VkImageViewCreateInfo msaaColorViewInfo{};
    msaaColorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    msaaColorViewInfo.image = m_gizmoMsaaColorImage;
    msaaColorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    msaaColorViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    msaaColorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    msaaColorViewInfo.subresourceRange.baseMipLevel = 0;
    msaaColorViewInfo.subresourceRange.levelCount = 1;
    msaaColorViewInfo.subresourceRange.baseArrayLayer = 0;
    msaaColorViewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &msaaColorViewInfo, nullptr, &m_gizmoMsaaColorView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo MSAA color image view!");
    }
    
    // Create MSAA depth image
    VkImageCreateInfo msaaDepthInfo{};
    msaaDepthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    msaaDepthInfo.imageType = VK_IMAGE_TYPE_2D;
    msaaDepthInfo.extent.width = m_selectionMaskExtent.width;
    msaaDepthInfo.extent.height = m_selectionMaskExtent.height;
    msaaDepthInfo.extent.depth = 1;
    msaaDepthInfo.mipLevels = 1;
    msaaDepthInfo.arrayLayers = 1;
    msaaDepthInfo.format = findDepthFormat();
    msaaDepthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    msaaDepthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    msaaDepthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    msaaDepthInfo.samples = GIZMO_MSAA_SAMPLES;
    msaaDepthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(m_device, &msaaDepthInfo, nullptr, &m_gizmoMsaaDepthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo MSAA depth image!");
    }
    
    VkMemoryRequirements msaaDepthMemReq;
    vkGetImageMemoryRequirements(m_device, m_gizmoMsaaDepthImage, &msaaDepthMemReq);
    
    VkMemoryAllocateInfo msaaDepthAllocInfo{};
    msaaDepthAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    msaaDepthAllocInfo.allocationSize = msaaDepthMemReq.size;
    msaaDepthAllocInfo.memoryTypeIndex = findMemoryType(msaaDepthMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &msaaDepthAllocInfo, nullptr, &m_gizmoMsaaDepthMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate gizmo MSAA depth memory!");
    }
    vkBindImageMemory(m_device, m_gizmoMsaaDepthImage, m_gizmoMsaaDepthMemory, 0);
    
    VkImageViewCreateInfo msaaDepthViewInfo{};
    msaaDepthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    msaaDepthViewInfo.image = m_gizmoMsaaDepthImage;
    msaaDepthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    msaaDepthViewInfo.format = findDepthFormat();
    msaaDepthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    msaaDepthViewInfo.subresourceRange.baseMipLevel = 0;
    msaaDepthViewInfo.subresourceRange.levelCount = 1;
    msaaDepthViewInfo.subresourceRange.baseArrayLayer = 0;
    msaaDepthViewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &msaaDepthViewInfo, nullptr, &m_gizmoMsaaDepthView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo MSAA depth image view!");
    }
    
    // Create gizmo resolve image (single-sample, for resolved gizmo)
    VkImageCreateInfo resolveInfo{};
    resolveInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    resolveInfo.imageType = VK_IMAGE_TYPE_2D;
    resolveInfo.extent.width = m_selectionMaskExtent.width;
    resolveInfo.extent.height = m_selectionMaskExtent.height;
    resolveInfo.extent.depth = 1;
    resolveInfo.mipLevels = 1;
    resolveInfo.arrayLayers = 1;
    resolveInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    resolveInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    resolveInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    resolveInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    resolveInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    resolveInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(m_device, &resolveInfo, nullptr, &m_gizmoResolveImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo resolve image!");
    }
    
    VkMemoryRequirements resolveMemReq;
    vkGetImageMemoryRequirements(m_device, m_gizmoResolveImage, &resolveMemReq);
    
    VkMemoryAllocateInfo resolveAllocInfo{};
    resolveAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    resolveAllocInfo.allocationSize = resolveMemReq.size;
    resolveAllocInfo.memoryTypeIndex = findMemoryType(resolveMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &resolveAllocInfo, nullptr, &m_gizmoResolveMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate gizmo resolve memory!");
    }
    vkBindImageMemory(m_device, m_gizmoResolveImage, m_gizmoResolveMemory, 0);
    
    VkImageViewCreateInfo resolveViewInfo{};
    resolveViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    resolveViewInfo.image = m_gizmoResolveImage;
    resolveViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    resolveViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    resolveViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    resolveViewInfo.subresourceRange.baseMipLevel = 0;
    resolveViewInfo.subresourceRange.levelCount = 1;
    resolveViewInfo.subresourceRange.baseArrayLayer = 0;
    resolveViewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &resolveViewInfo, nullptr, &m_gizmoResolveView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo resolve image view!");
    }
    
    // Create gizmo MSAA framebuffer (MSAA color, MSAA depth, resolve target)
    std::array<VkImageView, 3> gizmoMsaaAttachments = {m_gizmoMsaaColorView, m_gizmoMsaaDepthView, m_gizmoResolveView};
    
    VkFramebufferCreateInfo gizmoMsaaFramebufferInfo{};
    gizmoMsaaFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    gizmoMsaaFramebufferInfo.renderPass = m_gizmoMsaaRenderPass;
    gizmoMsaaFramebufferInfo.attachmentCount = static_cast<uint32_t>(gizmoMsaaAttachments.size());
    gizmoMsaaFramebufferInfo.pAttachments = gizmoMsaaAttachments.data();
    gizmoMsaaFramebufferInfo.width = m_selectionMaskExtent.width;
    gizmoMsaaFramebufferInfo.height = m_selectionMaskExtent.height;
    gizmoMsaaFramebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(m_device, &gizmoMsaaFramebufferInfo, nullptr, &m_gizmoMsaaFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo MSAA framebuffer!");
    }
    
    // Create gizmo blend framebuffer (renders to composite)
    VkFramebufferCreateInfo gizmoBlendFramebufferInfo{};
    gizmoBlendFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    gizmoBlendFramebufferInfo.renderPass = m_gizmoBlendRenderPass;
    gizmoBlendFramebufferInfo.attachmentCount = 1;
    gizmoBlendFramebufferInfo.pAttachments = &m_compositeImageView;
    gizmoBlendFramebufferInfo.width = m_selectionMaskExtent.width;
    gizmoBlendFramebufferInfo.height = m_selectionMaskExtent.height;
    gizmoBlendFramebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(m_device, &gizmoBlendFramebufferInfo, nullptr, &m_gizmoBlendFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo blend framebuffer!");
    }
    
    // Create sampler for resolved gizmo
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_gizmoResolveSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo resolve sampler!");
    }
}

void VulkanContext::createCompositePipeline() {
    // Helper to read shader file
    auto readFile = [](const std::string& filename) -> std::vector<char> {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + filename);
        }
        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    };
    
    // Create shader modules
    auto createShaderModule = [this](const std::vector<char>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        
        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module!");
        }
        return shaderModule;
    };
    
    // Load shaders
    auto fullscreenVert = readFile("shaders/fullscreen_vert.spv");
    auto compositeFrag = readFile("shaders/composite_frag.spv");
    
    VkShaderModule fullscreenVertModule = createShaderModule(fullscreenVert);
    VkShaderModule compositeFragModule = createShaderModule(compositeFrag);
    
    // Descriptor set layout: THREE combined image samplers (scene, selection mask, dilated mask)
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;
    
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;
    
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_compositeDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create composite descriptor set layout!");
    }
    
    // Push constant range: texelSize (vec2) + intensity (float) + pad (float) + outlineColor (vec4)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 8;  // vec2 + float + float + vec4
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_compositeDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_compositePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create composite pipeline layout!");
    }
    
    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = fullscreenVertModule;
    vertShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = compositeFragModule;
    fragShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };
    
    // No vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    
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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
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
    pipelineInfo.pDepthStencilState = nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_compositePipelineLayout;
    pipelineInfo.renderPass = m_dilationRenderPass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_compositePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create composite pipeline!");
    }
    
    // Cleanup shader modules
    vkDestroyShaderModule(m_device, compositeFragModule, nullptr);
    vkDestroyShaderModule(m_device, fullscreenVertModule, nullptr);
    
    CrashHandler::logInfo("Composite pipeline created successfully");
}

void VulkanContext::createGizmoBlendPipeline() {
    // Helper to read shader file
    auto readFile = [](const std::string& filename) -> std::vector<char> {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + filename);
        }
        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    };
    
    auto createShaderModule = [this](const std::vector<char>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        
        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module!");
        }
        return shaderModule;
    };
    
    // Load shaders - reuse fullscreen vert, need a simple blend frag
    auto fullscreenVert = readFile("shaders/fullscreen_vert.spv");
    auto blendFrag = readFile("shaders/gizmo_blend_frag.spv");
    
    VkShaderModule fullscreenVertModule = createShaderModule(fullscreenVert);
    VkShaderModule blendFragModule = createShaderModule(blendFrag);
    
    // Descriptor set layout: one combined image sampler for resolved gizmo
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_gizmoBlendDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo blend descriptor set layout!");
    }
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_gizmoBlendDescriptorSetLayout;
    
    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_gizmoBlendPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo blend pipeline layout!");
    }
    
    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = fullscreenVertModule;
    vertShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = blendFragModule;
    fragShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };
    
    // No vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
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
    
    // Alpha blending for compositing gizmo over scene
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
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
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_gizmoBlendPipelineLayout;
    pipelineInfo.renderPass = m_gizmoBlendRenderPass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_gizmoBlendPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create gizmo blend pipeline!");
    }
    
    vkDestroyShaderModule(m_device, blendFragModule, nullptr);
    vkDestroyShaderModule(m_device, fullscreenVertModule, nullptr);
    
    // Create descriptor set for sampling resolved gizmo
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_gizmoBlendDescriptorSetLayout;
    
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_gizmoBlendDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate gizmo blend descriptor set!");
    }
    
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_gizmoResolveSampler;
    imageInfo.imageView = m_gizmoResolveView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_gizmoBlendDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    
    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    
    CrashHandler::logInfo("Gizmo blend pipeline created successfully");
}

void VulkanContext::executeCompositePass(VkCommandBuffer commandBuffer, float intensity, float r, float g, float b) {
    struct CompositePushConstants {
        float texelSizeX;
        float texelSizeY;
        float intensity;
        float _pad;
        float outlineColorR;
        float outlineColorG;
        float outlineColorB;
        float outlineColorA;
    };
    
    CompositePushConstants pushConstants;
    pushConstants.texelSizeX = 1.0f / static_cast<float>(m_selectionMaskExtent.width);
    pushConstants.texelSizeY = 1.0f / static_cast<float>(m_selectionMaskExtent.height);
    pushConstants.intensity = intensity;
    pushConstants._pad = 0.0f;
    pushConstants.outlineColorR = r;
    pushConstants.outlineColorG = g;
    pushConstants.outlineColorB = b;
    pushConstants.outlineColorA = 1.0f;
    
    VkExtent2D extent = m_selectionMaskExtent;
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    
    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_dilationRenderPass;
    renderPassInfo.framebuffer = m_compositeFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipelineLayout, 0, 1, &m_compositeInputDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, m_compositePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompositePushConstants), &pushConstants);
    
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    
    vkCmdEndRenderPass(commandBuffer);
}

void VulkanContext::cleanupCompositeResources() {
    if (m_device == VK_NULL_HANDLE) return;
    
    // Cleanup composite pipeline
    vkDestroyPipeline(m_device, m_compositePipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_compositePipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_compositeDescriptorSetLayout, nullptr);
    
    vkDestroyFramebuffer(m_device, m_compositeFramebuffer, nullptr);
    vkDestroyImageView(m_device, m_compositeImageView, nullptr);
    vkDestroyImage(m_device, m_compositeImage, nullptr);
    vkFreeMemory(m_device, m_compositeImageMemory, nullptr);
    
    // Cleanup gizmo MSAA resources
    vkDestroyFramebuffer(m_device, m_gizmoMsaaFramebuffer, nullptr);
    vkDestroyImageView(m_device, m_gizmoMsaaColorView, nullptr);
    vkDestroyImage(m_device, m_gizmoMsaaColorImage, nullptr);
    vkFreeMemory(m_device, m_gizmoMsaaColorMemory, nullptr);
    vkDestroyImageView(m_device, m_gizmoMsaaDepthView, nullptr);
    vkDestroyImage(m_device, m_gizmoMsaaDepthImage, nullptr);
    vkFreeMemory(m_device, m_gizmoMsaaDepthMemory, nullptr);
    vkDestroyImageView(m_device, m_gizmoResolveView, nullptr);
    vkDestroyImage(m_device, m_gizmoResolveImage, nullptr);
    vkFreeMemory(m_device, m_gizmoResolveMemory, nullptr);
    vkDestroySampler(m_device, m_gizmoResolveSampler, nullptr);
    vkDestroyRenderPass(m_device, m_gizmoMsaaRenderPass, nullptr);
    
    // Cleanup gizmo blend resources
    vkDestroyFramebuffer(m_device, m_gizmoBlendFramebuffer, nullptr);
    vkDestroyPipeline(m_device, m_gizmoBlendPipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_gizmoBlendPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_gizmoBlendDescriptorSetLayout, nullptr);
    vkDestroyRenderPass(m_device, m_gizmoBlendRenderPass, nullptr);
}

void VulkanContext::createAssetPreviewResources() {
    CrashHandler::logInfo("Creating asset preview resources...");
    
    m_assetPreviewExtent = {1024, 1024};
    
    // Create color image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;  // Must match offscreen render pass format
    imageInfo.extent.width = m_assetPreviewExtent.width;
    imageInfo.extent.height = m_assetPreviewExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_assetPreviewImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create asset preview image!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_assetPreviewImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_assetPreviewImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate asset preview image memory!");
    }
    
    vkBindImageMemory(m_device, m_assetPreviewImage, m_assetPreviewImageMemory, 0);
    
    // Create color image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_assetPreviewImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;  // Must match offscreen render pass format
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_assetPreviewImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create asset preview image view!");
    }
    
    // Create depth image
    VkFormat depthFormat = findDepthFormat();
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.format = depthFormat;
    depthImageInfo.extent.width = m_assetPreviewExtent.width;
    depthImageInfo.extent.height = m_assetPreviewExtent.height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(m_device, &depthImageInfo, nullptr, &m_assetPreviewDepthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create asset preview depth image!");
    }
    
    VkMemoryRequirements depthMemRequirements;
    vkGetImageMemoryRequirements(m_device, m_assetPreviewDepthImage, &depthMemRequirements);
    
    VkMemoryAllocateInfo depthAllocInfo{};
    depthAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depthAllocInfo.allocationSize = depthMemRequirements.size;
    depthAllocInfo.memoryTypeIndex = findMemoryType(depthMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &depthAllocInfo, nullptr, &m_assetPreviewDepthMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate asset preview depth memory!");
    }
    
    vkBindImageMemory(m_device, m_assetPreviewDepthImage, m_assetPreviewDepthMemory, 0);
    
    // Create depth image view
    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_assetPreviewDepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = depthFormat;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &depthViewInfo, nullptr, &m_assetPreviewDepthView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create asset preview depth image view!");
    }
    
    // Create framebuffer (reuse offscreen render pass)
    std::array<VkImageView, 2> attachments = { m_assetPreviewImageView, m_assetPreviewDepthView };
    
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_offscreenRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = m_assetPreviewExtent.width;
    framebufferInfo.height = m_assetPreviewExtent.height;
    framebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_assetPreviewFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create asset preview framebuffer!");
    }
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_assetPreviewSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create asset preview sampler!");
    }
    
    // Transition image layout for shader read
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_assetPreviewImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    endSingleTimeCommands(commandBuffer);
    
    CrashHandler::logInfo("Asset preview resources created: " + std::to_string(m_assetPreviewExtent.width) + "x" + std::to_string(m_assetPreviewExtent.height));
}

void VulkanContext::initializeAssetPreviewDescriptor() {
    // Must be called after ImGui_ImplVulkan_Init()
    m_assetPreviewDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_assetPreviewSampler,
        m_assetPreviewImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    
    if (m_assetPreviewDescriptorSet == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create asset preview descriptor for ImGui!");
    }
    
    CrashHandler::logInfo("Asset preview descriptor initialized for ImGui");
}

void VulkanContext::cleanupAssetPreviewResources() {
    if (m_device == VK_NULL_HANDLE) return;
    
    vkDestroySampler(m_device, m_assetPreviewSampler, nullptr);
    vkDestroyFramebuffer(m_device, m_assetPreviewFramebuffer, nullptr);
    vkDestroyImageView(m_device, m_assetPreviewDepthView, nullptr);
    vkDestroyImage(m_device, m_assetPreviewDepthImage, nullptr);
    vkFreeMemory(m_device, m_assetPreviewDepthMemory, nullptr);
    vkDestroyImageView(m_device, m_assetPreviewImageView, nullptr);
    vkDestroyImage(m_device, m_assetPreviewImage, nullptr);
    vkFreeMemory(m_device, m_assetPreviewImageMemory, nullptr);
}

void VulkanContext::requestAssetPreviewResize(uint32_t width, uint32_t height) {
    // Clamp to reasonable bounds (use parentheses to avoid Windows max/min macro conflict)
    width = (std::max)(64u, (std::min)(width, 4096u));
    height = (std::max)(64u, (std::min)(height, 4096u));
    
    // Skip if size hasn't changed
    if (m_assetPreviewExtent.width == width && m_assetPreviewExtent.height == height) {
        return;
    }
    
    // Queue the resize for processing before next frame
    m_assetPreviewResizePending = true;
    m_pendingAssetPreviewWidth = width;
    m_pendingAssetPreviewHeight = height;
}

void VulkanContext::processPendingAssetPreviewResize() {
    if (!m_assetPreviewResizePending) {
        return;
    }
    
    m_assetPreviewResizePending = false;
    uint32_t width = m_pendingAssetPreviewWidth;
    uint32_t height = m_pendingAssetPreviewHeight;
    
    // Double-check size hasn't changed to current
    if (m_assetPreviewExtent.width == width && m_assetPreviewExtent.height == height) {
        return;
    }
    
    // Wait for device to be idle before recreating resources
    vkDeviceWaitIdle(m_device);
    
    CrashHandler::logInfo("Resizing asset preview: " + std::to_string(width) + "x" + std::to_string(height));
    
    // Remove old ImGui descriptor BEFORE destroying the image view it references
    if (m_assetPreviewDescriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_assetPreviewDescriptorSet);
        m_assetPreviewDescriptorSet = VK_NULL_HANDLE;
    }
    
    // Clean up old resources (except sampler which can be reused)
    vkDestroyFramebuffer(m_device, m_assetPreviewFramebuffer, nullptr);
    vkDestroyImageView(m_device, m_assetPreviewDepthView, nullptr);
    vkDestroyImage(m_device, m_assetPreviewDepthImage, nullptr);
    vkFreeMemory(m_device, m_assetPreviewDepthMemory, nullptr);
    vkDestroyImageView(m_device, m_assetPreviewImageView, nullptr);
    vkDestroyImage(m_device, m_assetPreviewImage, nullptr);
    vkFreeMemory(m_device, m_assetPreviewImageMemory, nullptr);
    
    // Update extent
    m_assetPreviewExtent = {width, height};
    
    // Recreate color image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;  // Must match offscreen render pass format
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_assetPreviewImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to recreate asset preview image!");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_assetPreviewImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_assetPreviewImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate asset preview image memory!");
    }
    
    vkBindImageMemory(m_device, m_assetPreviewImage, m_assetPreviewImageMemory, 0);
    
    // Recreate color image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_assetPreviewImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;  // Must match offscreen render pass format
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_assetPreviewImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to recreate asset preview image view!");
    }
    
    // Recreate depth image
    VkFormat depthFormat = findDepthFormat();
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.format = depthFormat;
    depthImageInfo.extent.width = width;
    depthImageInfo.extent.height = height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (vkCreateImage(m_device, &depthImageInfo, nullptr, &m_assetPreviewDepthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to recreate asset preview depth image!");
    }
    
    VkMemoryRequirements depthMemRequirements;
    vkGetImageMemoryRequirements(m_device, m_assetPreviewDepthImage, &depthMemRequirements);
    
    VkMemoryAllocateInfo depthAllocInfo{};
    depthAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depthAllocInfo.allocationSize = depthMemRequirements.size;
    depthAllocInfo.memoryTypeIndex = findMemoryType(depthMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(m_device, &depthAllocInfo, nullptr, &m_assetPreviewDepthMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate asset preview depth memory!");
    }
    
    vkBindImageMemory(m_device, m_assetPreviewDepthImage, m_assetPreviewDepthMemory, 0);
    
    // Recreate depth image view
    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_assetPreviewDepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = depthFormat;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(m_device, &depthViewInfo, nullptr, &m_assetPreviewDepthView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to recreate asset preview depth image view!");
    }
    
    // Recreate framebuffer
    std::array<VkImageView, 2> attachments = { m_assetPreviewImageView, m_assetPreviewDepthView };
    
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_offscreenRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_assetPreviewFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to recreate asset preview framebuffer!");
    }
    
    // Transition image layout for shader read
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_assetPreviewImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    endSingleTimeCommands(commandBuffer);
    
    // Update ImGui descriptor with new image view
    m_assetPreviewDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_assetPreviewSampler,
        m_assetPreviewImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    
    CrashHandler::logInfo("Asset preview resized to: " + std::to_string(width) + "x" + std::to_string(height));
}


} // namespace Propulsion
