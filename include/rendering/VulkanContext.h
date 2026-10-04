#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <optional>

namespace Forge {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanContext {
public:
    VulkanContext(GLFWwindow* window);
    ~VulkanContext();

    bool waitForFrame();  // Wait for fence and acquire image - safe to update descriptors after this
    void beginFrame();    // Begin command buffer recording
    void beginMainRenderPass();  // Start the main swap chain render pass
    void endFrame();
    void waitIdle();
    void recreateSwapChain(int width, int height);

    // Getters
    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue getPresentQueue() const { return m_presentQueue; }
    VkSurfaceKHR getSurface() const { return m_surface; }
    VkSwapchainKHR getSwapChain() const { return m_swapChain; }
    VkFormat getSwapChainImageFormat() const { return m_swapChainImageFormat; }
    VkExtent2D getSwapChainExtent() const { return m_swapChainExtent; }
    VkRenderPass getRenderPass() const { return m_renderPass; }
    VkCommandBuffer getCurrentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }
    VkFramebuffer getCurrentFramebuffer() const { return m_swapChainFramebuffers[m_imageIndex]; }
    uint32_t getCurrentFrame() const { return m_currentFrame; }
    uint32_t getImageIndex() const { return m_imageIndex; }
    VkCommandPool getCommandPool() const { return m_commandPool; }
    VkDescriptorPool getDescriptorPool() const { return m_descriptorPool; }
    const std::vector<VkImage>& getSwapChainImages() const { return m_swapChainImages; }
    const std::vector<VkImageView>& getSwapChainImageViews() const { return m_swapChainImageViews; }
    QueueFamilyIndices getQueueFamilies() const { return m_queueFamilies; }
    
    // Offscreen rendering getters
    VkRenderPass getOffscreenRenderPass() const { return m_offscreenRenderPass; }
    VkFramebuffer getOffscreenFramebuffer() const { return m_offscreenFramebuffer; }
    VkImage getOffscreenImage() const { return m_offscreenImage; }
    VkImageView getOffscreenImageView() const { return m_offscreenImageView; }
    VkImageView getOffscreenDepthView() const { return m_offscreenDepthView; }
    VkSampler getOffscreenSampler() const { return m_offscreenSampler; }
    VkDescriptorSet getOffscreenDescriptorSet() const { return m_offscreenDescriptorSet; }
    VkExtent2D getOffscreenExtent() const { return m_offscreenExtent; }
    
    // Selection mask getters
    VkRenderPass getSelectionMaskRenderPass() const { return m_selectionMaskRenderPass; }
    VkFramebuffer getSelectionMaskFramebuffer() const { return m_selectionMaskFramebuffer; }
    VkImage getSelectionMaskImage() const { return m_selectionMaskImage; }
    VkImageView getSelectionMaskImageView() const { return m_selectionMaskImageView; }
    VkSampler getSelectionMaskSampler() const { return m_selectionMaskSampler; }
    VkDescriptorSet getSelectionMaskDescriptorSet() const { return m_selectionMaskDescriptorSet; }
    VkExtent2D getSelectionMaskExtent() const { return m_selectionMaskExtent; }
    
    // Dilation pass getters (TempMask = horizontal dilated, DilatedMask = fully dilated)
    VkRenderPass getDilationRenderPass() const { return m_dilationRenderPass; }
    VkFramebuffer getTempMaskFramebuffer() const { return m_tempMaskFramebuffer; }
    VkFramebuffer getDilatedMaskFramebuffer() const { return m_dilatedMaskFramebuffer; }
    VkImageView getTempMaskImageView() const { return m_tempMaskImageView; }
    VkImageView getDilatedMaskImageView() const { return m_dilatedMaskImageView; }
    VkDescriptorSet getTempMaskDescriptorSet() const { return m_tempMaskDescriptorSet; }
    VkDescriptorSet getDilatedMaskDescriptorSet() const { return m_dilatedMaskDescriptorSet; }
    VkDescriptorSet getSelectionMaskSamplerDescriptor() const { return m_selectionMaskSamplerDescriptor; }
    VkDescriptorSet getTempMaskSamplerDescriptor() const { return m_tempMaskSamplerDescriptor; }
    VkPipeline getDilateHPipeline() const { return m_dilateHPipeline; }
    VkPipeline getDilateVPipeline() const { return m_dilateVPipeline; }
    VkPipelineLayout getDilationPipelineLayout() const { return m_dilationPipelineLayout; }
    
    // Composite pass getters
    VkFramebuffer getCompositeFramebuffer() const { return m_compositeFramebuffer; }
    VkDescriptorSet getCompositeDescriptorSet() const { return m_compositeDescriptorSet; }
    VkPipeline getCompositePipeline() const { return m_compositePipeline; }
    VkPipelineLayout getCompositePipelineLayout() const { return m_compositePipelineLayout; }
    
    // Gizmo MSAA pass getters
    VkRenderPass getGizmoMsaaRenderPass() const { return m_gizmoMsaaRenderPass; }
    VkFramebuffer getGizmoMsaaFramebuffer() const { return m_gizmoMsaaFramebuffer; }
    VkSampleCountFlagBits getGizmoMsaaSamples() const { return GIZMO_MSAA_SAMPLES; }
    
    // Gizmo blend pass getters
    VkRenderPass getGizmoBlendRenderPass() const { return m_gizmoBlendRenderPass; }
    VkFramebuffer getGizmoBlendFramebuffer() const { return m_gizmoBlendFramebuffer; }
    VkPipeline getGizmoBlendPipeline() const { return m_gizmoBlendPipeline; }
    VkPipelineLayout getGizmoBlendPipelineLayout() const { return m_gizmoBlendPipelineLayout; }
    VkDescriptorSet getGizmoBlendDescriptorSet() const { return m_gizmoBlendDescriptorSet; }
    
    // Asset preview getters
    VkRenderPass getAssetPreviewRenderPass() const { return m_offscreenRenderPass; } // Reuse offscreen render pass
    VkFramebuffer getAssetPreviewFramebuffer() const { return m_assetPreviewFramebuffer; }
    VkDescriptorSet getAssetPreviewDescriptorSet() const { return m_assetPreviewDescriptorSet; }
    VkExtent2D getAssetPreviewExtent() const { return m_assetPreviewExtent; }
    void requestAssetPreviewResize(uint32_t width, uint32_t height);  // Queue resize for next frame
    void processPendingAssetPreviewResize();  // Call BEFORE beginFrame()
    
    // Execute dilation passes (call after mask render pass, before main render pass)
    void executeDilationPasses(VkCommandBuffer commandBuffer, int radius = 5, float viewportAspectRatio = 1.0f);
    
    // Execute composite pass (call after dilation, extracts outline and blends over scene)
    void executeCompositePass(VkCommandBuffer commandBuffer, float intensity = 1.0f, float r = 1.0f, float g = 0.5f, float b = 0.0f);
    
    // Must be called after ImGui_ImplVulkan_Init()
    void initializeOffscreenDescriptor();
    void initializeSelectionMaskDescriptor();
    void initializeDilationDescriptors();
    void initializeAssetPreviewDescriptor();

    // Helper functions
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat findDepthFormat();
    
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createDescriptorPool();
    void createDepthResources();
    void cleanupSwapChain();
    
    // Offscreen rendering
    void createOffscreenResources();
    void cleanupOffscreenResources();
    void createOffscreenRenderPass();
    void createOffscreenFramebuffer();
    void createOffscreenDescriptorSet();
    
    // Selection mask
    void createSelectionMaskResources();
    void cleanupSelectionMaskResources();
    void createSelectionMaskRenderPass();
    void createSelectionMaskFramebuffer();
    
    // Dilation passes
    void createDilationResources();
    void cleanupDilationResources();
    void createDilationRenderPass();
    void createDilationFramebuffers();
    void createDilationPipelines();
    
    // Composite pass and gizmo MSAA rendering
    void createCompositeResources();
    void cleanupCompositeResources();
    void createGizmoMsaaRenderPass();
    void createGizmoBlendRenderPass();
    void createCompositeFramebuffer();
    void createCompositePipeline();
    void createGizmoBlendPipeline();
    
    // Asset preview
    void createAssetPreviewResources();
    void cleanupAssetPreviewResources();

    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    bool isDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    GLFWwindow* m_window;
    VkInstance m_instance;
    VkDebugUtilsMessengerEXT m_debugMessenger;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    QueueFamilyIndices m_queueFamilies;
    VkQueue m_graphicsQueue;
    VkQueue m_presentQueue;
    VkSurfaceKHR m_surface;
    VkSwapchainKHR m_swapChain;
    std::vector<VkImage> m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;
    VkFormat m_swapChainImageFormat;
    VkExtent2D m_swapChainExtent;
    VkRenderPass m_renderPass;
    std::vector<VkFramebuffer> m_swapChainFramebuffers;
    VkCommandPool m_commandPool;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;  // One per frame in flight
    std::vector<VkSemaphore> m_renderFinishedSemaphores;  // One per swapchain image
    std::vector<VkFence> m_inFlightFences;
    VkDescriptorPool m_descriptorPool;
    
    // Depth buffer resources
    VkImage m_depthImage;
    VkDeviceMemory m_depthImageMemory;
    VkImageView m_depthImageView;
    
    // Offscreen rendering resources
    VkRenderPass m_offscreenRenderPass;
    VkImage m_offscreenImage;
    VkDeviceMemory m_offscreenImageMemory;
    VkImageView m_offscreenImageView;
    VkImage m_offscreenDepthImage;
    VkDeviceMemory m_offscreenDepthMemory;
    VkImageView m_offscreenDepthView;
    VkFramebuffer m_offscreenFramebuffer;
    VkSampler m_offscreenSampler;
    VkDescriptorSet m_offscreenDescriptorSet;
    VkExtent2D m_offscreenExtent;
    
    // Selection mask resources
    VkRenderPass m_selectionMaskRenderPass;
    VkImage m_selectionMaskImage;
    VkDeviceMemory m_selectionMaskImageMemory;
    VkImageView m_selectionMaskImageView;
    VkImage m_selectionMaskDepthImage;
    VkDeviceMemory m_selectionMaskDepthMemory;
    VkImageView m_selectionMaskDepthView;
    VkFramebuffer m_selectionMaskFramebuffer;
    VkSampler m_selectionMaskSampler;
    VkDescriptorSet m_selectionMaskDescriptorSet;
    VkExtent2D m_selectionMaskExtent;
    
    // Dilation resources (TempMask = after horizontal pass, DilatedMask = after vertical pass)
    VkRenderPass m_dilationRenderPass;
    VkImage m_tempMaskImage;
    VkDeviceMemory m_tempMaskImageMemory;
    VkImageView m_tempMaskImageView;
    VkFramebuffer m_tempMaskFramebuffer;
    VkImage m_dilatedMaskImage;
    VkDeviceMemory m_dilatedMaskImageMemory;
    VkImageView m_dilatedMaskImageView;
    VkFramebuffer m_dilatedMaskFramebuffer;
    VkDescriptorSet m_tempMaskDescriptorSet;        // For ImGui display
    VkDescriptorSet m_dilatedMaskDescriptorSet;     // For ImGui display
    VkDescriptorSet m_selectionMaskSamplerDescriptor; // For sampling selection mask in dilation
    VkDescriptorSet m_tempMaskSamplerDescriptor;      // For sampling temp mask in vertical pass
    
    // Dilation pipeline resources
    VkDescriptorSetLayout m_dilationDescriptorSetLayout;
    VkPipelineLayout m_dilationPipelineLayout;
    VkPipeline m_dilateHPipeline;  // Horizontal dilation
    VkPipeline m_dilateVPipeline;  // Vertical dilation
    
    // Composite resources (final output with outline over scene)
    VkImage m_compositeImage;
    VkDeviceMemory m_compositeImageMemory;
    VkImageView m_compositeImageView;
    VkFramebuffer m_compositeFramebuffer;
    VkDescriptorSet m_compositeDescriptorSet;       // For ImGui display
    VkDescriptorSetLayout m_compositeDescriptorSetLayout;
    VkPipelineLayout m_compositePipelineLayout;
    VkPipeline m_compositePipeline;
    VkDescriptorSet m_compositeInputDescriptorSet;  // For sampling scene + masks
    
    // Gizmo MSAA render pass (renders gizmo with MSAA, resolves to gizmo image)
    VkRenderPass m_gizmoMsaaRenderPass;
    VkImage m_gizmoMsaaColorImage;
    VkDeviceMemory m_gizmoMsaaColorMemory;
    VkImageView m_gizmoMsaaColorView;
    VkImage m_gizmoMsaaDepthImage;
    VkDeviceMemory m_gizmoMsaaDepthMemory;
    VkImageView m_gizmoMsaaDepthView;
    VkImage m_gizmoResolveImage;          // Resolved gizmo (single-sample)
    VkDeviceMemory m_gizmoResolveMemory;
    VkImageView m_gizmoResolveView;
    VkFramebuffer m_gizmoMsaaFramebuffer;
    static constexpr VkSampleCountFlagBits GIZMO_MSAA_SAMPLES = VK_SAMPLE_COUNT_4_BIT;
    
    // Gizmo blend pass (blends resolved gizmo onto composite)
    VkRenderPass m_gizmoBlendRenderPass;
    VkFramebuffer m_gizmoBlendFramebuffer;
    VkPipeline m_gizmoBlendPipeline;
    VkPipelineLayout m_gizmoBlendPipelineLayout;
    VkDescriptorSetLayout m_gizmoBlendDescriptorSetLayout;
    VkDescriptorSet m_gizmoBlendDescriptorSet;  // For sampling resolved gizmo
    VkSampler m_gizmoResolveSampler;

    // Asset preview resources (for Asset Editor tab)
    VkImage m_assetPreviewImage;
    VkDeviceMemory m_assetPreviewImageMemory;
    VkImageView m_assetPreviewImageView;
    VkImage m_assetPreviewDepthImage;
    VkDeviceMemory m_assetPreviewDepthMemory;
    VkImageView m_assetPreviewDepthView;
    VkFramebuffer m_assetPreviewFramebuffer;
    VkSampler m_assetPreviewSampler;
    VkDescriptorSet m_assetPreviewDescriptorSet;
    VkExtent2D m_assetPreviewExtent;
    
    // Deferred asset preview resize (to avoid resizing during frame recording)
    bool m_assetPreviewResizePending = false;
    uint32_t m_pendingAssetPreviewWidth = 0;
    uint32_t m_pendingAssetPreviewHeight = 0;

    uint32_t m_currentFrame;
    uint32_t m_imageIndex;
    bool m_framebufferResized;

#ifdef _DEBUG
    const bool m_enableValidationLayers = true;
#else
    const bool m_enableValidationLayers = false;
#endif

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};

} // namespace Forge
