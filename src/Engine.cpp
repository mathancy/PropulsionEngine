#include "Engine.h"
#include "project/ProjectSerializer.h"
#include "rendering/SVGRenderer.h"
#include "rendering/MeshLoader.h"
#include "nodegraph/NodeGraph.h"
#include "platform/FileDialogs.h"
#include "ui/ImGuiTheme.h"
#include "utils/Utils.h"
#include "scripting/PlayMode.h"
#include "project/ObjectFile.h"
#include <vulkan/vulkan.h>
#include <chrono>
#include <thread>
#include <map>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "ui/IconsFontAwesome7.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdlib>

namespace Forge {

Engine::Engine(int width, int height)
    : m_viewportWidth(width), m_viewportHeight(height),
      m_viewportPos(0, 0), m_viewportSize(0, 0),
      m_viewportHovered(false), m_viewportFocused(false),
      m_viewMode(ViewMode::Composite),
      m_firstMouse(true), m_lastX(0.0f), m_lastY(0.0f),
      m_rightMousePressed(false), m_deltaTime(0.0f), m_lastFrame(0.0f),
      m_showViewport(true), m_showFPS(false), m_showObjectsPanel(true), m_showContentBrowser(true),
      m_showWorldGrid(true), m_firstFrame(true), m_dockingLayoutInitialized(false),
      m_lastDockSpaceSize(0, 0), m_selectedObjectIndex(-1), m_selectedContentIndex(-1),
      m_showMaterialNamePopup(false), m_showObjectNamePopup(false),
      m_isDraggingObject(false), m_draggedObjectIndex(-1), m_dragPreviewPosition(0.0f),
      m_gizmoMode(GizmoMode::None), m_hoveredGizmoAxis(GizmoAxis::None),
      m_activeGizmoAxis(GizmoAxis::None), m_gizmoActive(false),
      m_gizmoStartPos(0.0f), m_gizmoStartScale(1.0f), m_gizmoStartQuaternion(1.0f, 0.0f, 0.0f, 0.0f), m_gizmoInteractionStart(0.0f),
      m_dragStartMouseWorld(0.0f), m_gizmoStartAxisT(0.0f), m_rotationAccumulatedAngle(0.0f),
      // TODO: Initialize Vulkan icon textures
      m_isLoggingRotation(false), m_rotationLogFrameCount(0), m_activeTabIndex(0), m_tabBarHeight(0.0f),
      m_currentProjectPath(""), m_projectModified(false),
      m_showProjectNameDialog(false), m_isNewProject(true),
      m_showStartupDialog(true), m_engineInitialized(false),
      m_showPreferencesWindow(false), m_autoExposure(true), m_exposure(1.0f), 
      m_targetExposure(1.0f), m_exposureAdaptSpeed(2.0f),
      m_outlineColor(1.0f, 0.85f, 0.4f), m_outlineThickness(2),
      // TODO: Initialize logo texture
      m_logoAnimationTime(0.0f),
      m_targetFrameTime(1.0f / 240.0f), // 240 FPS cap
      m_fontRegular(nullptr), m_fontBold(nullptr), m_fontItalic(nullptr), m_fontBoldItalic(nullptr),
      m_currentFolder(""), m_selectedFolderIndex(0),
      m_showRenamePopup(false), m_renamingItemIndex(-1), m_renamingFolder(false), m_renamingFolderIndex(-1),
      m_showExitConfirmation(false), m_exitConfirmed(false),
      m_showTabCloseConfirmation(false), m_tabToClose(-1),
      m_showImportDialog(false) {
    
    CrashHandler::logInfo("Engine constructor started");
    
    // Initialize project name buffer
    memset(m_projectNameBuffer, 0, sizeof(m_projectNameBuffer));
    strcpy_s(m_projectNameBuffer, "NewProject");
    
#ifdef _WIN32
    // Hide console window by default
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow != nullptr) {
        ShowWindow(consoleWindow, SW_HIDE);
    }
#endif
    
    m_window = std::make_unique<Window>(width, height, "Forge Engine");
    CrashHandler::logInfo("Window created successfully");
    
    // Create Vulkan context AFTER window creation
    try {
        m_vulkanContext = std::make_unique<VulkanContext>(m_window->getNativeWindow());
        CrashHandler::logInfo("Vulkan context created successfully");
    } catch (const std::exception& e) {
        std::string error = "Failed to create Vulkan context: " + std::string(e.what());
        CrashHandler::logError(error);
        throw;
    }
    
    // Initialize camera with Z-up coordinate system (Blender-style)
    // Position in positive X/Y/Z, looking at origin with 45° downward angle
    m_camera = std::make_unique<Camera>(glm::vec3(7.0f, 7.0f, 5.0f), glm::vec3(0.0f, 0.0f, 1.0f), -135.0f, -30.0f);
    CrashHandler::logInfo("Camera created successfully");
    
    init();
    loadToolIcons();
    
    CrashHandler::logInfo("Engine constructor completed successfully");
}

Engine::~Engine() {
    shutdown();
}

void Engine::init() {
    CrashHandler::logInfo("Engine::init() started");
    
    // Create Vulkan shaders using SPIR-V
    try {
        m_shader = std::make_unique<Shader>(m_vulkanContext.get(), "shaders/basic_vert.spv", "shaders/basic_frag.spv", VK_NULL_HANDLE, VK_CULL_MODE_BACK_BIT, true, true);
        CrashHandler::logInfo("Main shader created with texture support");
        
        // Create shader specifically for offscreen rendering with texture support
        m_offscreenShader = std::make_unique<Shader>(m_vulkanContext.get(), "shaders/basic_vert.spv", "shaders/basic_frag.spv", m_vulkanContext->getOffscreenRenderPass(), VK_CULL_MODE_BACK_BIT, true, true);
        CrashHandler::logInfo("Offscreen shader created with texture support");
        
        // Create shader for selection mask rendering
        m_maskShader = std::make_unique<Shader>(m_vulkanContext.get(), "shaders/mask_vert.spv", "shaders/mask_frag.spv", m_vulkanContext->getSelectionMaskRenderPass());
        CrashHandler::logInfo("Selection mask shader created");
        
        // Create gizmo shader with push constants, 4x MSAA, and depth testing - uses MSAA render pass
        m_gizmoShader = Shader::createGizmoShader(m_vulkanContext.get(), "shaders/gizmo_vert.spv", "shaders/gizmo_frag.spv", m_vulkanContext->getGizmoMsaaRenderPass(), m_vulkanContext->getGizmoMsaaSamples());
        CrashHandler::logInfo("Gizmo shader created with 4x MSAA and depth testing");
    } catch (const std::exception& e) {
        std::string error = "Failed to create shaders: " + std::string(e.what());
        CrashHandler::logError(error);
        throw;
    }
    
    // Load default meshes from FBX files
    CrashHandler::logInfo("Loading default meshes from FBX files...");
    m_cube = MeshLoader::loadFromFile(m_vulkanContext.get(), "resources/default_meshes/Cube.fbx");
    if (!m_cube) {
        CrashHandler::logError("Failed to load Cube.fbx, falling back to procedural mesh");
        m_cube = std::unique_ptr<Mesh>(Mesh::createCube(m_vulkanContext.get()));
    }
    CrashHandler::logInfo("Cube mesh loaded");
    
    m_sphere = MeshLoader::loadFromFile(m_vulkanContext.get(), "resources/default_meshes/Sphere.fbx");
    if (!m_sphere) {
        CrashHandler::logError("Failed to load Sphere.fbx, falling back to procedural mesh");
        m_sphere = std::unique_ptr<Mesh>(Mesh::createSphere(m_vulkanContext.get()));
    }
    CrashHandler::logInfo("Sphere mesh loaded");
    
    m_cylinder = MeshLoader::loadFromFile(m_vulkanContext.get(), "resources/default_meshes/Cylinder.fbx");
    if (!m_cylinder) {
        CrashHandler::logError("Failed to load Cylinder.fbx, falling back to procedural mesh");
        m_cylinder = std::unique_ptr<Mesh>(Mesh::createCylinder(m_vulkanContext.get()));
    }
    CrashHandler::logInfo("Cylinder mesh loaded");
    
    // Create procedural meshes for gizmos (guaranteed unit size for consistent gizmo rendering)
    CrashHandler::logInfo("Creating procedural gizmo meshes...");
    m_gizmoCube = std::unique_ptr<Mesh>(Mesh::createCube(m_vulkanContext.get()));
    m_gizmoCone = std::unique_ptr<Mesh>(Mesh::createCone(m_vulkanContext.get(), 16));
    // Torus with tube radius 0.025 to match translate gizmo shaft thickness
    m_gizmoTorus = std::unique_ptr<Mesh>(Mesh::createTorus(m_vulkanContext.get(), 1.0f, 0.025f, 48, 12));
    m_gizmoDisc = std::unique_ptr<Mesh>(Mesh::createDisc(m_vulkanContext.get(), 360));
    CrashHandler::logInfo("Procedural gizmo meshes created");
    
    // Initialize world grid with Vulkan
    m_worldGrid = std::make_unique<WorldGrid>(m_vulkanContext.get());
    m_worldGrid->setGridSpacing(1.0f);
    m_worldGrid->setFadeDistance(400.0f);
    m_worldGrid->setGridColor(glm::vec3(0.4f, 0.4f, 0.4f));
    m_worldGrid->setAxisXColor(glm::vec3(1.0f, 0.2f, 0.2f));
    m_worldGrid->setAxisYColor(glm::vec3(0.2f, 1.0f, 0.2f));
    CrashHandler::logInfo("World grid initialized with Vulkan");
    
    // Vulkan uses swap chain for rendering - no custom framebuffer needed
    CrashHandler::logInfo("Using Vulkan swap chain for viewport rendering");
    
    // Initialize ImGui
    CrashHandler::logInfo("Initializing ImGui...");
    initImGui();
    CrashHandler::logInfo("ImGui initialized");
    
    // Initialize thumbnail generator (after ImGui)
    CrashHandler::logInfo("Initializing thumbnail generator...");
    m_thumbnailGenerator = std::make_unique<ThumbnailGenerator>(m_vulkanContext.get());
    if (!m_thumbnailGenerator->initialize()) {
        CrashHandler::logError("Failed to initialize thumbnail generator");
    }
    CrashHandler::logInfo("Thumbnail generator initialized");
    
    // Initialize scene objects
    m_sceneObjects.push_back(SceneObject("Cube_01"));
    
    // Initialize offscreen and selection mask descriptors
    m_vulkanContext->initializeOffscreenDescriptor();
    m_vulkanContext->initializeSelectionMaskDescriptor();
    m_vulkanContext->initializeDilationDescriptors();
    m_vulkanContext->initializeAssetPreviewDescriptor();
    
    // Initialize content items
    m_contentItems.push_back(ContentItem("Cube", "Mesh"));
    m_contentItems.push_back(ContentItem("Sphere", "Mesh"));
    m_contentItems.push_back(ContentItem("Cylinder", "Mesh"));
    
    // Generate thumbnails
    generateThumbnail(m_contentItems[0], m_cube.get());
    generateThumbnail(m_contentItems[1], m_sphere.get());
    generateThumbnail(m_contentItems[2], m_cylinder.get());
    
    // Initialize tabs with default scene tab
    m_tabs.push_back(Tab("Untitled Scene", TabType::Scene));
    
    // Set up callbacks
    m_window->setResizeCallback([this](int w, int h) { onResize(w, h); });
    m_window->setContentScaleCallback([this](float) { applyUIScale(); });  // Window moved to a monitor with different DPI
    m_window->setMouseButtonCallback([this](int btn, int action, int mods) { onMouseButton(btn, action, mods); });
    m_window->setMouseMoveCallback([this](double x, double y) { onMouseMove(x, y); });
    m_window->setCloseCallback([this]() { onWindowClose(); });
}

void Engine::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    // Load Space Mono fonts with Font Awesome icons merged
    float fontSize = 16.0f;
    
    // Merge Font Awesome icons config
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = 16.0f; // Match the font size
    
    // Load Space Mono Regular with Font Awesome merged
    m_fontRegular = io.Fonts->AddFontFromFileTTF("resources/fonts/SpaceMonoRegular-JRrmm.ttf", fontSize);
    std::string fontPath = "resources/fonts/Font Awesome 7 Free-Solid-900.otf";
    if (std::filesystem::exists(fontPath)) {
        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize, &icons_config, icons_ranges);
    } else {
        std::cout << "Warning: Font Awesome font not found at " << fontPath << std::endl;
        std::cout << "Icon rendering will not work. Download Font Awesome 7 Free and place 'Font Awesome 7 Free-Solid-900.otf' in resources/fonts/" << std::endl;
    }
    
    // Load other Space Mono variants (without icons for now)
    m_fontBold = io.Fonts->AddFontFromFileTTF("resources/fonts/SpaceMonoBold-AL8np.ttf", fontSize);
    m_fontItalic = io.Fonts->AddFontFromFileTTF("resources/fonts/SpaceMonoItalic-VGdll.ttf", fontSize);
    m_fontBoldItalic = io.Fonts->AddFontFromFileTTF("resources/fonts/SpaceMonoBoldItalic-qZxv1.ttf", fontSize);
    
    // Load default font as fallback
    io.Fonts->AddFontDefault();
    
    // Apply frosted glass theme, then scale it for the display (4K / high-DPI monitors)
    ForgeEngine::UI::setupFrostedGlassTheme();
    loadPreferences();
    applyUIScale();

    // Add startup message to console
    m_consoleBuffer.push_back("ForgeEngine initialized successfully with Vulkan.");
    
    // Initialize ImGui for Vulkan
    ImGui_ImplGlfw_InitForVulkan(m_window->getNativeWindow(), true);
    
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_3;
    init_info.Instance = m_vulkanContext->getInstance();
    init_info.PhysicalDevice = m_vulkanContext->getPhysicalDevice();
    init_info.Device = m_vulkanContext->getDevice();
    init_info.QueueFamily = m_vulkanContext->getQueueFamilies().graphicsFamily.value();
    init_info.Queue = m_vulkanContext->getGraphicsQueue();
    init_info.DescriptorPool = m_vulkanContext->getDescriptorPool();
    init_info.MinImageCount = 2;
    init_info.ImageCount = (uint32_t)m_vulkanContext->getSwapChainImages().size();
    init_info.PipelineCache = VK_NULL_HANDLE;
    
    // Set main viewport pipeline info
    init_info.PipelineInfoMain.RenderPass = m_vulkanContext->getRenderPass();
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    
    ImGui_ImplVulkan_Init(&init_info);
    
    CrashHandler::logInfo("ImGui initialized for Vulkan");
    
    // Initialize offscreen descriptor set (requires ImGui to be initialized first)
    m_vulkanContext->initializeOffscreenDescriptor();
}

void Engine::applyUIScale() {
    // Auto: follow the OS display scale of the monitor the window is on. Manual: the user's preference.
    float scale = m_uiScaleAuto ? m_window->getContentScale() : m_uiScalePref;

    ForgeEngine::UI::setUIScale(scale);
    CrashHandler::logInfo("UI scale set to " + std::to_string(ForgeEngine::UI::getUIScale()));
}

static const char* kPreferencesFile = "preferences.ini";

void Engine::loadPreferences() {
    std::ifstream file(kPreferencesFile);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        if (key == "ui_scale_auto") {
            m_uiScaleAuto = (value != "0");
        } else if (key == "ui_scale") {
            float parsed = static_cast<float>(std::atof(value.c_str()));
            if (parsed > 0.0f) {
                m_uiScalePref = std::clamp(parsed, 0.5f, 3.0f);
            }
        }
    }
}

void Engine::savePreferences() const {
    std::ofstream file(kPreferencesFile, std::ios::trunc);
    if (!file.is_open()) {
        CrashHandler::logError("Failed to write preferences file");
        return;
    }
    file << "ui_scale_auto=" << (m_uiScaleAuto ? 1 : 0) << "\n";
    file << "ui_scale=" << m_uiScalePref << "\n";
}

void Engine::loadToolIcons() {
    // TODO: Implement Vulkan texture loading for tool icons
    // This requires:
    // 1. Load image data with stbi_load
    // 2. Create VkImage with VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    // 3. Create staging buffer and copy image data
    // 4. Transition image layout to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    // 5. Create VkImageView
    // 6. Create VkDescriptorSet with ImGui_ImplVulkan_AddTexture()
    
    CrashHandler::logInfo("Tool icon loading temporarily disabled during Vulkan migration");
}

void Engine::run() {
    CrashHandler::logInfo("Engine::run() started - entering main loop");
    
    // Logo will be animated per-frame in renderStartupDialog
    
    while (!m_window->shouldClose()) {
        float currentFrame = static_cast<float>(glfwGetTime());
        m_deltaTime = currentFrame - m_lastFrame;
        
        // FPS cap at 240 - sleep if frame finished too quickly
        if (m_deltaTime < m_targetFrameTime) {
            float sleepTime = m_targetFrameTime - m_deltaTime;
            std::this_thread::sleep_for(std::chrono::duration<float>(sleepTime));
            currentFrame = static_cast<float>(glfwGetTime());
            m_deltaTime = currentFrame - m_lastFrame;
        }
        
        m_lastFrame = currentFrame;
        
        processInput(m_deltaTime);
        update(m_deltaTime);
        render();
        
        m_window->pollEvents();
    }
    
    CrashHandler::logInfo("Engine::run() - main loop exited normally");
}


void Engine::startPlay() {
    if (m_isPlaying) return;

    m_playSnapshot = m_sceneObjects;
    m_playMode = std::make_unique<PlayMode>(m_window->getNativeWindow(),
                                            [this](const std::string& line) { m_consoleBuffer.push_back(line); });

    // An Object asset is the "class": every instance placed from it runs the asset's script.
    // Objects with no script (or no source asset) just exist and are rendered; nothing runs for them.
    for (SceneObject& obj : m_sceneObjects) {
        obj.scriptFile.clear();
        if (obj.sourceObjectGUID.empty()) continue;
        for (const ContentItem& item : m_contentItems) {
            if (item.type == "Object" && item.id == obj.sourceObjectGUID) {
                obj.scriptFile = item.scriptFile;
                break;
            }
        }
    }

    // Scripts are files in the project's Scripts folder, read now, so an edit needs no rebuild.
    std::string projectDir = std::filesystem::path(m_currentProjectPath).parent_path().string();
    if (!m_playMode->start(m_sceneObjects, projectDir)) {
        m_playMode.reset();
        m_sceneObjects = m_playSnapshot;  // nothing was started; leave the scene as it was
        m_playSnapshot.clear();
        m_consoleBuffer.push_back("Play cancelled: fix the script problems above and press Play again.");
        return;
    }
    m_consoleBuffer.push_back("Play started: " + std::to_string(m_playMode->runningCount()) + " scripted object(s) of " +
                              std::to_string(m_sceneObjects.size()) + ". Press Escape or Stop to end.");
    m_isPlaying = true;
    m_gizmoActive = false;
}

void Engine::stopPlay() {
    if (!m_isPlaying) return;
    m_isPlaying = false;
    m_playMode.reset();
    m_sceneObjects = m_playSnapshot;  // put the scene back exactly as it was
    m_playSnapshot.clear();
    m_consoleBuffer.push_back("Play stopped; the scene was restored.");
}

void Engine::update(float deltaTime) {
    if (m_isPlaying && m_playMode) {
        bool typing = ImGui::GetIO().WantTextInput;
        m_playMode->step(deltaTime, m_sceneObjects, !typing);
    }

    // Update auto-exposure
    if (m_autoExposure) {
        m_targetExposure = calculateSceneLuminance();
    }
    
    // Smoothly adapt exposure towards target
    m_exposure += (m_targetExposure - m_exposure) * m_exposureAdaptSpeed * deltaTime;
}

void Engine::render() {
    // Skip rendering if window is minimized (width or height is 0)
    int width, height;
    glfwGetFramebufferSize(m_window->getNativeWindow(), &width, &height);
    if (width == 0 || height == 0) {
        return;  // Window is minimized, skip rendering
    }
    
    // Wait for frame fence and acquire swapchain image
    // After this call, it's safe to update descriptor sets for this frame
    if (!m_vulkanContext->waitForFrame()) {
        return;  // Swapchain was recreated, skip this frame
    }
    
    // Process any pending asset preview resize BEFORE beginning command buffer
    // This must happen when the device is idle (after waitForFrame)
    m_vulkanContext->processPendingAssetPreviewResize();
    
    // Update descriptor sets BEFORE beginning command buffer recording
    // This is required because you cannot update a descriptor set while it's bound to a recording command buffer
    if (!m_showStartupDialog && m_showViewport) {
        // Reset all texture bindings to default before rendering
        m_offscreenShader->unbindAllTextures(m_vulkanContext->getCurrentFrame());
        
        // Pre-bind all textures needed by scene objects
        for (size_t i = 0; i < m_sceneObjects.size(); i++) {
            SceneObject& obj = m_sceneObjects[i];
            if (!obj.visible) continue;
            
            MaterialRenderInfo renderInfo = getMaterialRenderInfo(obj);
            
            // Bind each texture type to its corresponding binding slot
            if (renderInfo.useAlbedoTexture && !renderInfo.albedoTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.albedoTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    m_offscreenShader->bindTexture(m_vulkanContext->getCurrentFrame(), 
                                                   tex->view, tex->sampler, 1);
                }
            }
            if (renderInfo.useMetallicTexture && !renderInfo.metallicTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.metallicTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    m_offscreenShader->bindTexture(m_vulkanContext->getCurrentFrame(), 
                                                   tex->view, tex->sampler, 2);
                }
            }
            if (renderInfo.useRoughnessTexture && !renderInfo.roughnessTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.roughnessTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    m_offscreenShader->bindTexture(m_vulkanContext->getCurrentFrame(), 
                                                   tex->view, tex->sampler, 3);
                }
            }
            if (renderInfo.useNormalTexture && !renderInfo.normalTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.normalTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    m_offscreenShader->bindTexture(m_vulkanContext->getCurrentFrame(), 
                                                   tex->view, tex->sampler, 4);
                }
            }
            if (renderInfo.useAOTexture && !renderInfo.aoTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.aoTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    m_offscreenShader->bindTexture(m_vulkanContext->getCurrentFrame(), 
                                                   tex->view, tex->sampler, 5);
                }
            }
            if (renderInfo.useEmissiveTexture && !renderInfo.emissiveTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.emissiveTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    m_offscreenShader->bindTexture(m_vulkanContext->getCurrentFrame(), 
                                                   tex->view, tex->sampler, 6);
                }
            }
            if (renderInfo.useHeightTexture && !renderInfo.heightTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.heightTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    m_offscreenShader->bindTexture(m_vulkanContext->getCurrentFrame(), 
                                                   tex->view, tex->sampler, 7);
                }
            }
            // Only bind textures from first visible object that has them
            break;
        }
    }
    
    // Pre-bind textures for asset preview (if asset editor tab is active)
    if (m_activeTabIndex >= 0 && m_activeTabIndex < static_cast<int>(m_tabs.size())) {
        Tab& activeTab = m_tabs[m_activeTabIndex];
        if (activeTab.type == TabType::AssetEditor) {
            // Evaluate material and pre-bind textures for asset preview
            if (activeTab.contentItemIndex >= 0 && activeTab.contentItemIndex < static_cast<int>(m_contentItems.size())) {
                ContentItem& item = m_contentItems[activeTab.contentItemIndex];
                if (item.type == "Material" && activeTab.nodeGraph) {
                    EvaluatedMaterial evalMat = activeTab.nodeGraph->evaluateMaterial();
                    uint32_t frame = m_vulkanContext->getCurrentFrame();
                    
                    // Albedo texture (binding 1)
                    if (evalMat.useAlbedoTexture && !evalMat.albedoTexturePath.empty()) {
                        CachedTexture* tex = getOrLoadTexture(evalMat.albedoTexturePath);
                        if (tex && tex->view != VK_NULL_HANDLE) {
                            m_offscreenShader->bindTexture(frame, tex->view, tex->sampler, 1);
                        }
                    }
                    // Metallic texture (binding 2)
                    if (evalMat.useMetallicTexture && !evalMat.metallicTexturePath.empty()) {
                        CachedTexture* tex = getOrLoadTexture(evalMat.metallicTexturePath);
                        if (tex && tex->view != VK_NULL_HANDLE) {
                            m_offscreenShader->bindTexture(frame, tex->view, tex->sampler, 2);
                        }
                    }
                    // Roughness texture (binding 3)
                    if (evalMat.useRoughnessTexture && !evalMat.roughnessTexturePath.empty()) {
                        CachedTexture* tex = getOrLoadTexture(evalMat.roughnessTexturePath);
                        if (tex && tex->view != VK_NULL_HANDLE) {
                            m_offscreenShader->bindTexture(frame, tex->view, tex->sampler, 3);
                        }
                    }
                    // Normal texture (binding 4)
                    if (evalMat.useNormalTexture && !evalMat.normalTexturePath.empty()) {
                        CachedTexture* tex = getOrLoadTexture(evalMat.normalTexturePath);
                        if (tex && tex->view != VK_NULL_HANDLE) {
                            m_offscreenShader->bindTexture(frame, tex->view, tex->sampler, 4);
                        }
                    }
                    // AO texture (binding 5)
                    if (evalMat.useAOTexture && !evalMat.aoTexturePath.empty()) {
                        CachedTexture* tex = getOrLoadTexture(evalMat.aoTexturePath);
                        if (tex && tex->view != VK_NULL_HANDLE) {
                            m_offscreenShader->bindTexture(frame, tex->view, tex->sampler, 5);
                        }
                    }
                    // Emissive texture (binding 6)
                    if (evalMat.useEmissiveTexture && !evalMat.emissiveTexturePath.empty()) {
                        CachedTexture* tex = getOrLoadTexture(evalMat.emissiveTexturePath);
                        if (tex && tex->view != VK_NULL_HANDLE) {
                            m_offscreenShader->bindTexture(frame, tex->view, tex->sampler, 6);
                        }
                    }
                    // Height texture (binding 7)
                    if (evalMat.useHeightTexture && !evalMat.heightTexturePath.empty()) {
                        CachedTexture* tex = getOrLoadTexture(evalMat.heightTexturePath);
                        if (tex && tex->view != VK_NULL_HANDLE) {
                            m_offscreenShader->bindTexture(frame, tex->view, tex->sampler, 7);
                        }
                    }
                }
            }
        }
    }
    
    // Now begin command buffer recording
    m_vulkanContext->beginFrame();
    
    VkCommandBuffer commandBuffer = m_vulkanContext->getCurrentCommandBuffer();
    
    // STEP 1: Render 3D scene to offscreen framebuffer
    if (!m_showStartupDialog && m_showViewport) {
        
        VkRenderPassBeginInfo offscreenRenderPassInfo{};
        offscreenRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        offscreenRenderPassInfo.renderPass = m_vulkanContext->getOffscreenRenderPass();
        offscreenRenderPassInfo.framebuffer = m_vulkanContext->getOffscreenFramebuffer();
        offscreenRenderPassInfo.renderArea.offset = {0, 0};
        offscreenRenderPassInfo.renderArea.extent = {2048, 2048};
        
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.15f, 0.15f, 0.15f, 1.0f}};  // Dark grey background
        clearValues[1].depthStencil = {1.0f, 0};
        offscreenRenderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        offscreenRenderPassInfo.pClearValues = clearValues.data();
        
        vkCmdBeginRenderPass(commandBuffer, &offscreenRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        // Set viewport for offscreen rendering
        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = 2048.0f;
        viewport.height = 2048.0f;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        
        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = {2048, 2048};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        
        // Set up matrices with correct aspect ratio
        float aspectRatio = 2048.0f / 2048.0f;  // Offscreen texture is square
        if (m_viewportSize.x > 0 && m_viewportSize.y > 0) {
            aspectRatio = m_viewportSize.x / m_viewportSize.y;
        }
        glm::mat4 projection = m_camera->getProjectionMatrix(aspectRatio);
        glm::mat4 view = m_camera->getViewMatrix();
    
        // Bind offscreen shader pipeline for scene objects
        m_offscreenShader->bind(commandBuffer);
        
        // Reset dynamic UBO index for new frame
        m_offscreenShader->resetObjectIndex();
    
        // Render all scene objects
        uint32_t objectDrawIndex = 0;
        for (size_t i = 0; i < m_sceneObjects.size(); i++) {
            SceneObject& obj = m_sceneObjects[i];
            if (!obj.visible) continue;
            
            // Build model matrix
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, obj.position);
            model = model * glm::mat4_cast(obj.quaternion);
            model = glm::scale(model, obj.scale);
            
            // Determine which mesh to render (GUID-based lookup first)
            Mesh* meshToRender = m_cube.get();
            if (!obj.meshGUID.empty()) {
                // GUID-based lookup (stable across rescans)
                for (const auto& meshItem : m_contentItems) {
                    if (meshItem.type == "Mesh" && meshItem.id == obj.meshGUID) {
                        if (meshItem.name == "Sphere") meshToRender = m_sphere.get();
                        else if (meshItem.name == "Cylinder") meshToRender = m_cylinder.get();
                        // Cube is default, no need to check
                        break;
                    }
                }
            } else if (obj.meshIndex >= 0 && obj.meshIndex < static_cast<int>(m_contentItems.size())) {
                // Legacy index-based lookup
                const auto& meshItem = m_contentItems[obj.meshIndex];
                if (meshItem.name == "Sphere") {
                    meshToRender = m_sphere.get();
                } else if (meshItem.name == "Cylinder") {
                    meshToRender = m_cylinder.get();
                }
            } else {
                // Last resort: name-based fallback
                if (obj.name.find("Sphere") != std::string::npos) {
                    meshToRender = m_sphere.get();
                } else if (obj.name.find("Cylinder") != std::string::npos) {
                    meshToRender = m_cylinder.get();
                }
            }
            
            // Get material render info - evaluates node graph if material is assigned
            MaterialRenderInfo renderInfo = getMaterialRenderInfo(obj);
            
            // Build the PBR Uniform Buffer Object
            UniformBufferObject ubo = {};
            ubo.model = model;
            ubo.view = view;
            ubo.projection = projection;
            ubo.colorAndExposure = glm::vec4(renderInfo.baseColor, m_exposure);
            
            // Material parameters (defaults if not textured)
            ubo.materialParams = glm::vec4(renderInfo.metallic, renderInfo.roughness, renderInfo.ao, 0.0f);
            
            // Texture flags - will be adjusted based on what's actually loaded
            float useAlbedo = 0.0f, useMetallic = 0.0f, useRoughness = 0.0f, useNormal = 0.0f;
            float useAO = 0.0f, useEmissive = 0.0f, useHeight = 0.0f;
            
            // Check and bind each texture
            if (renderInfo.useAlbedoTexture && !renderInfo.albedoTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.albedoTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    useAlbedo = 1.0f;
                }
            }
            if (renderInfo.useMetallicTexture && !renderInfo.metallicTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.metallicTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    useMetallic = 1.0f;
                }
            }
            if (renderInfo.useRoughnessTexture && !renderInfo.roughnessTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.roughnessTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    useRoughness = 1.0f;
                }
            }
            if (renderInfo.useNormalTexture && !renderInfo.normalTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.normalTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    useNormal = 1.0f;
                }
            }
            if (renderInfo.useAOTexture && !renderInfo.aoTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.aoTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    useAO = 1.0f;
                }
            }
            if (renderInfo.useEmissiveTexture && !renderInfo.emissiveTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.emissiveTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    useEmissive = 1.0f;
                }
            }
            if (renderInfo.useHeightTexture && !renderInfo.heightTexturePath.empty()) {
                CachedTexture* tex = getOrLoadTexture(renderInfo.heightTexturePath);
                if (tex && tex->view != VK_NULL_HANDLE) {
                    useHeight = 1.0f;
                }
            }
            
            ubo.textureFlags = glm::vec4(useAlbedo, useMetallic, useRoughness, useNormal);
            ubo.textureFlags2 = glm::vec4(useAO, useEmissive, useHeight, 0.0f);
            ubo.invertFlags = glm::vec4(
                renderInfo.invertMetallic ? 1.0f : 0.0f,
                renderInfo.invertRoughness ? 1.0f : 0.0f,
                renderInfo.invertNormal ? 1.0f : 0.0f,
                renderInfo.invertAO ? 1.0f : 0.0f
            );
            ubo.textureStrengths = glm::vec4(
                renderInfo.metallicStrength,
                renderInfo.roughnessStrength,
                renderInfo.normalStrength,
                renderInfo.aoStrength
            );
            
            // Emissive color (RGB = color, A = emissive texture strength)
            ubo.emissiveColor = glm::vec4(renderInfo.emissiveColor, renderInfo.emissiveStrength);
            
            // Height map parameters (x=scale, y=invert)
            ubo.heightParams = glm::vec4(
                renderInfo.heightStrength,
                renderInfo.invertHeight ? 1.0f : 0.0f,
                0.0f,
                0.0f
            );
            
            // Camera position for specular
            ubo.cameraPos = m_camera->getPosition();
            
            // Update dynamic UBO at current object index and bind with proper offset
            m_offscreenShader->updateUniformBufferDynamic(m_vulkanContext->getCurrentFrame(), ubo);
            m_offscreenShader->bindWithDynamicOffset(commandBuffer, m_vulkanContext->getCurrentFrame(), objectDrawIndex);
            meshToRender->draw(commandBuffer);
            objectDrawIndex++;
        }
        
        // Render world grid AFTER all opaque scene objects
        // This allows proper depth testing - grid will be occluded by objects in front
        // and will occlude objects behind the Z=0 plane
        if (m_showWorldGrid && m_worldGrid) {
            m_worldGrid->render(commandBuffer, m_camera.get(), aspectRatio);
        }
        
        vkCmdEndRenderPass(commandBuffer);
    }
    
    // STEP 2: Render selection mask (always render to keep it updated)
    if (!m_showStartupDialog && m_showViewport) {
        VkRenderPassBeginInfo maskRenderPassInfo{};
        maskRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        maskRenderPassInfo.renderPass = m_vulkanContext->getSelectionMaskRenderPass();
        maskRenderPassInfo.framebuffer = m_vulkanContext->getSelectionMaskFramebuffer();
        maskRenderPassInfo.renderArea.offset = {0, 0};
        maskRenderPassInfo.renderArea.extent = m_vulkanContext->getSelectionMaskExtent();
        
        // Clear mask to black with full alpha
        std::array<VkClearValue, 2> maskClearValues{};
        maskClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        maskClearValues[1].depthStencil = {1.0f, 0};
        maskRenderPassInfo.clearValueCount = static_cast<uint32_t>(maskClearValues.size());
        maskRenderPassInfo.pClearValues = maskClearValues.data();
        
        vkCmdBeginRenderPass(commandBuffer, &maskRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        // Set viewport for mask rendering
        VkExtent2D maskExtent = m_vulkanContext->getSelectionMaskExtent();
        VkViewport maskViewport = {};
        maskViewport.x = 0.0f;
        maskViewport.y = 0.0f;
        maskViewport.width = static_cast<float>(maskExtent.width);
        maskViewport.height = static_cast<float>(maskExtent.height);
        maskViewport.minDepth = 0.0f;
        maskViewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &maskViewport);
        
        VkRect2D maskScissor = {};
        maskScissor.offset = {0, 0};
        maskScissor.extent = maskExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &maskScissor);
        
        // Only draw into mask if an object is selected
        if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
            // Set up matrices (same as scene)
            float aspectRatio = static_cast<float>(maskExtent.width) / static_cast<float>(maskExtent.height);
            if (m_viewportSize.x > 0 && m_viewportSize.y > 0) {
                aspectRatio = m_viewportSize.x / m_viewportSize.y;
            }
            glm::mat4 projection = m_camera->getProjectionMatrix(aspectRatio);
            glm::mat4 view = m_camera->getViewMatrix();
            
            // Bind mask shader pipeline
            m_maskShader->bind(commandBuffer);
            
            // Reset dynamic UBO index for mask pass
            m_maskShader->resetObjectIndex();
            
            // Render only the selected object
            SceneObject& selectedObj = m_sceneObjects[m_selectedObjectIndex];
            
            // Build model matrix
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, selectedObj.position);
            model = model * glm::mat4_cast(selectedObj.quaternion);
            model = glm::scale(model, selectedObj.scale);
            
            // Determine which mesh to render (GUID-based lookup first)
            Mesh* meshToRender = m_cube.get();
            if (!selectedObj.meshGUID.empty()) {
                // GUID-based lookup (stable across rescans)
                for (const auto& meshItem : m_contentItems) {
                    if (meshItem.type == "Mesh" && meshItem.id == selectedObj.meshGUID) {
                        if (meshItem.name == "Sphere") meshToRender = m_sphere.get();
                        else if (meshItem.name == "Cylinder") meshToRender = m_cylinder.get();
                        break;
                    }
                }
            } else if (selectedObj.meshIndex >= 0 && selectedObj.meshIndex < static_cast<int>(m_contentItems.size())) {
                // Legacy index-based lookup
                const auto& meshItem = m_contentItems[selectedObj.meshIndex];
                if (meshItem.name == "Sphere") {
                    meshToRender = m_sphere.get();
                } else if (meshItem.name == "Cylinder") {
                    meshToRender = m_cylinder.get();
                }
            } else {
                // Last resort: name-based fallback
                if (selectedObj.name.find("Sphere") != std::string::npos) {
                    meshToRender = m_sphere.get();
                } else if (selectedObj.name.find("Cylinder") != std::string::npos) {
                    meshToRender = m_cylinder.get();
                }
            }
            
            // Render the selected object into the mask
            UniformBufferObject ubo = {};
            ubo.model = model;
            ubo.view = view;
            ubo.projection = projection;
            ubo.colorAndExposure = glm::vec4(1.0f); // Not used by mask shader
            ubo.materialParams = glm::vec4(0.0f, 0.5f, 1.0f, 0.0f);  // Defaults
            ubo.textureFlags = glm::vec4(0.0f);     // No textures for mask
            ubo.textureFlags2 = glm::vec4(0.0f);
            ubo.invertFlags = glm::vec4(0.0f);
            ubo.textureStrengths = glm::vec4(1.0f);
            ubo.emissiveColor = glm::vec4(0.0f);
            ubo.heightParams = glm::vec4(0.0f);     // No height for mask
            ubo.cameraPos = m_camera->getPosition();
            
            // Update dynamic UBO and bind with offset 0 (single object for mask)
            m_maskShader->updateUniformBufferDynamic(m_vulkanContext->getCurrentFrame(), ubo);
            m_maskShader->bindWithDynamicOffset(commandBuffer, m_vulkanContext->getCurrentFrame(), 0);
            meshToRender->draw(commandBuffer);
        }
        
        vkCmdEndRenderPass(commandBuffer);
        
        // STEP 2.5: Execute dilation passes (horizontal + vertical)
        // Pass viewport aspect ratio to ensure consistent outline thickness
        float viewportAspectRatio = m_viewportSize.x / m_viewportSize.y;
        m_vulkanContext->executeDilationPasses(commandBuffer, m_outlineThickness, viewportAspectRatio);
        
        // STEP 2.6: Execute composite pass (extracts outline and blends over scene)
        m_vulkanContext->executeCompositePass(commandBuffer, 1.0f, m_outlineColor.r, m_outlineColor.g, m_outlineColor.b);
        
        // STEP 2.7: Render gizmo on top of everything (2-pass MSAA approach)
        if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
            VkExtent2D gizmoExtent = m_vulkanContext->getSelectionMaskExtent();
            
            // PASS 1: Render gizmo to MSAA buffer, resolve to gizmo image
            VkRenderPassBeginInfo msaaPassInfo{};
            msaaPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            msaaPassInfo.renderPass = m_vulkanContext->getGizmoMsaaRenderPass();
            msaaPassInfo.framebuffer = m_vulkanContext->getGizmoMsaaFramebuffer();
            msaaPassInfo.renderArea.offset = {0, 0};
            msaaPassInfo.renderArea.extent = gizmoExtent;
            
            // Clear values: MSAA color (transparent), MSAA depth, resolve target (don't care)
            std::array<VkClearValue, 3> msaaClearValues{};
            msaaClearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // MSAA color - transparent
            msaaClearValues[1].depthStencil = {1.0f, 0};  // MSAA depth
            msaaClearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Resolve target
            msaaPassInfo.clearValueCount = static_cast<uint32_t>(msaaClearValues.size());
            msaaPassInfo.pClearValues = msaaClearValues.data();
            
            vkCmdBeginRenderPass(commandBuffer, &msaaPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            
            // Set viewport and scissor
            VkViewport gizmoViewport{};
            gizmoViewport.x = 0.0f;
            gizmoViewport.y = 0.0f;
            gizmoViewport.width = static_cast<float>(gizmoExtent.width);
            gizmoViewport.height = static_cast<float>(gizmoExtent.height);
            gizmoViewport.minDepth = 0.0f;
            gizmoViewport.maxDepth = 1.0f;
            vkCmdSetViewport(commandBuffer, 0, 1, &gizmoViewport);
            
            VkRect2D gizmoScissor{};
            gizmoScissor.offset = {0, 0};
            gizmoScissor.extent = gizmoExtent;
            vkCmdSetScissor(commandBuffer, 0, 1, &gizmoScissor);
            
            // Calculate matrices and render gizmo
            float aspectRatio = m_viewportSize.x / m_viewportSize.y;
            glm::mat4 projection = m_camera->getProjectionMatrix(aspectRatio);
            glm::mat4 view = m_camera->getViewMatrix();
            
            renderGizmo(commandBuffer, view, projection);
            
            vkCmdEndRenderPass(commandBuffer);
            
            // PASS 2: Blend resolved gizmo onto composite
            VkRenderPassBeginInfo blendPassInfo{};
            blendPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            blendPassInfo.renderPass = m_vulkanContext->getGizmoBlendRenderPass();
            blendPassInfo.framebuffer = m_vulkanContext->getGizmoBlendFramebuffer();
            blendPassInfo.renderArea.offset = {0, 0};
            blendPassInfo.renderArea.extent = gizmoExtent;
            
            // No clear needed - we load existing composite
            VkClearValue blendClearValue{};
            blendClearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
            blendPassInfo.clearValueCount = 1;
            blendPassInfo.pClearValues = &blendClearValue;
            
            vkCmdBeginRenderPass(commandBuffer, &blendPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            
            vkCmdSetViewport(commandBuffer, 0, 1, &gizmoViewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &gizmoScissor);
            
            // Bind blend pipeline and draw fullscreen quad
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vulkanContext->getGizmoBlendPipeline());
            VkDescriptorSet blendDescSet = m_vulkanContext->getGizmoBlendDescriptorSet();
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                m_vulkanContext->getGizmoBlendPipelineLayout(), 0, 1, 
                &blendDescSet, 0, nullptr);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);  // Fullscreen triangle
            
            vkCmdEndRenderPass(commandBuffer);
        }
    }
    
    // STEP 2.8: Render asset preview for asset editor tabs
    if (m_activeTabIndex >= 0 && m_activeTabIndex < static_cast<int>(m_tabs.size())) {
        Tab& activeTab = m_tabs[m_activeTabIndex];
        if (activeTab.type == TabType::AssetEditor) {
            renderAssetPreview(activeTab, commandBuffer);
        }
    }
    
    // STEP 3: Begin main render pass for UI
    m_vulkanContext->beginMainRenderPass();
    
    // Render ImGui UI (which displays the offscreen texture in the viewport)
    renderUI();
    
    // End the frame and present
    m_vulkanContext->endFrame();
}

glm::vec3 Engine::getMaterialColorForObject(const SceneObject& obj) const {
    // If no material assigned, return the object's direct color
    const int materialIdx = materialIndexOf(obj);
    if (materialIdx < 0) {
        return obj.color;
    }
    
    const auto& materialItem = m_contentItems[materialIdx];
    
    // Only use SAVED material data - evaluate from stored ContentItem nodes/connections
    // This ensures scene only updates when material is saved, not during live editing
    
    // Find the MaterialOutput node (usually id 1)
    int outputNodeId = -1;
    for (const auto& nodeData : materialItem.nodes) {
        if (nodeData.nodeType == "MaterialOutput") {
            outputNodeId = nodeData.nodeId;
            break;
        }
    }
    
    if (outputNodeId < 0) {
        return materialItem.materialColor;  // Fallback to stored color
    }
    
    // Find connection to Base Color input (pin id 1)
    int sourceNodeId = -1;
    for (const auto& conn : materialItem.connections) {
        if (conn.inputNodeId == outputNodeId && conn.inputPinId == 1) {
            sourceNodeId = conn.outputNodeId;
            break;
        }
    }
    
    if (sourceNodeId < 0) {
        return materialItem.materialColor;  // No connection to Base Color
    }
    
    // Find the source node and get its color
    for (const auto& nodeData : materialItem.nodes) {
        if (nodeData.nodeId == sourceNodeId && nodeData.nodeType == "ColorWheel") {
            return nodeData.color;
        }
    }
    
    return materialItem.materialColor;  // Fallback
}

Engine::MaterialRenderInfo Engine::getMaterialRenderInfo(const SceneObject& obj) const {
    MaterialRenderInfo info;
    // Defaults are set in the struct definition
    
    // If no material assigned, return the object's direct color
    const int materialIdx = materialIndexOf(obj);
    if (materialIdx < 0) {
        info.baseColor = obj.color;
        return info;
    }
    
    const auto& materialItem = m_contentItems[materialIdx];
    
    // Find the MaterialOutput node
    int outputNodeId = -1;
    for (const auto& nodeData : materialItem.nodes) {
        if (nodeData.nodeType == "MaterialOutput") {
            outputNodeId = nodeData.nodeId;
            break;
        }
    }
    
    if (outputNodeId < 0) {
        info.baseColor = materialItem.materialColor;
        return info;
    }
    
    // Build a map of connections: (inputNodeId, inputPinId) -> sourceNodeId
    std::map<std::pair<int, int>, int> connectionMap;
    for (const auto& conn : materialItem.connections) {
        connectionMap[{conn.inputNodeId, conn.inputPinId}] = conn.outputNodeId;
    }
    
    // Build a map of nodes by ID
    std::map<int, const MaterialNodeData*> nodeMap;
    for (const auto& nodeData : materialItem.nodes) {
        nodeMap[nodeData.nodeId] = &nodeData;
    }
    
    // Helper lambda to get source node for a given pin on output node
    auto getSourceNode = [&](int pinId) -> const MaterialNodeData* {
        auto it = connectionMap.find({outputNodeId, pinId});
        if (it == connectionMap.end()) return nullptr;
        auto nodeIt = nodeMap.find(it->second);
        if (nodeIt == nodeMap.end()) return nullptr;
        return nodeIt->second;
    };
    
    // Pin 1: Base Color
    if (auto* node = getSourceNode(1)) {
        if (node->nodeType == "ColorWheel") {
            info.baseColor = node->color;
        } else if (node->nodeType == "Image" && !node->texturePath.empty()) {
            info.useAlbedoTexture = true;
            info.albedoTexturePath = node->texturePath;
            info.baseColor = glm::vec3(1.0f);
        }
    } else {
        info.baseColor = materialItem.materialColor;
    }
    
    // Pin 2: Metallic
    if (auto* node = getSourceNode(2)) {
        if (node->nodeType == "Image" && !node->texturePath.empty()) {
            info.useMetallicTexture = true;
            info.metallicTexturePath = node->texturePath;
            info.metallicStrength = node->strength;
            info.invertMetallic = node->invertTexture;
        }
    }
    
    // Pin 3: Roughness
    if (auto* node = getSourceNode(3)) {
        if (node->nodeType == "Image" && !node->texturePath.empty()) {
            info.useRoughnessTexture = true;
            info.roughnessTexturePath = node->texturePath;
            info.roughnessStrength = node->strength;
            info.invertRoughness = node->invertTexture;
        }
    }
    
    // Pin 5: Normal
    if (auto* node = getSourceNode(5)) {
        if (node->nodeType == "Image" && !node->texturePath.empty()) {
            info.useNormalTexture = true;
            info.normalTexturePath = node->texturePath;
            info.normalStrength = node->strength;
            info.invertNormal = node->invertTexture;
        }
    }
    
    // Pin 6: Emissive Color
    if (auto* node = getSourceNode(6)) {
        if (node->nodeType == "ColorWheel") {
            info.emissiveColor = node->color;
        } else if (node->nodeType == "Image" && !node->texturePath.empty()) {
            info.useEmissiveTexture = true;
            info.emissiveTexturePath = node->texturePath;
            info.emissiveStrength = node->strength;
        }
    }
    
    // Pin 8: Ambient Occlusion
    if (auto* node = getSourceNode(8)) {
        if (node->nodeType == "Image" && !node->texturePath.empty()) {
            info.useAOTexture = true;
            info.aoTexturePath = node->texturePath;
            info.aoStrength = node->strength;
            info.invertAO = node->invertTexture;
        }
    }
    
    return info;
}

Engine::CachedTexture* Engine::getOrLoadTexture(const std::string& path) {
    // Check if already cached
    auto it = m_textureCache.find(path);
    if (it != m_textureCache.end()) {
        return &it->second;
    }
    
    // Load texture from file
    int width, height, channels;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    
    if (!pixels) {
        CrashHandler::logError("Failed to load texture: " + path);
        return nullptr;
    }
    
    CrashHandler::logInfo("Loading texture: " + path + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    
    VkDevice device = m_vulkanContext->getDevice();
    VkDeviceSize imageSize = width * height * 4;
    
    CachedTexture tex;
    tex.width = width;
    tex.height = height;
    
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        stbi_image_free(pixels);
        return nullptr;
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        stbi_image_free(pixels);
        return nullptr;
    }
    
    vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);
    
    // Copy pixel data to staging buffer
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);
    
    stbi_image_free(pixels);
    
    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &tex.image) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return nullptr;
    }
    
    VkMemoryRequirements imgMemReq;
    vkGetImageMemoryRequirements(device, tex.image, &imgMemReq);
    
    VkMemoryAllocateInfo imgAllocInfo{};
    imgAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imgAllocInfo.allocationSize = imgMemReq.size;
    imgAllocInfo.memoryTypeIndex = m_vulkanContext->findMemoryType(imgMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &imgAllocInfo, nullptr, &tex.memory) != VK_SUCCESS) {
        vkDestroyImage(device, tex.image, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return nullptr;
    }
    
    vkBindImageMemory(device, tex.image, tex.memory, 0);
    
    // Transition and copy
    VkCommandBuffer commandBuffer = m_vulkanContext->beginSingleTimeCommands();
    
    // Transition to transfer destination
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
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
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    m_vulkanContext->endSingleTimeCommands(commandBuffer);
    
    // Clean up staging buffer
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
        vkDestroyImage(device, tex.image, nullptr);
        vkFreeMemory(device, tex.memory, nullptr);
        return nullptr;
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
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &tex.sampler) != VK_SUCCESS) {
        vkDestroyImageView(device, tex.view, nullptr);
        vkDestroyImage(device, tex.image, nullptr);
        vkFreeMemory(device, tex.memory, nullptr);
        return nullptr;
    }
    
    // Cache and return
    m_textureCache[path] = tex;
    return &m_textureCache[path];
}

void Engine::cleanupTextureCache() {
    VkDevice device = m_vulkanContext->getDevice();
    for (auto& pair : m_textureCache) {
        CachedTexture& tex = pair.second;
        if (tex.sampler != VK_NULL_HANDLE) vkDestroySampler(device, tex.sampler, nullptr);
        if (tex.view != VK_NULL_HANDLE) vkDestroyImageView(device, tex.view, nullptr);
        if (tex.image != VK_NULL_HANDLE) vkDestroyImage(device, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE) vkFreeMemory(device, tex.memory, nullptr);
    }
    m_textureCache.clear();
}

void Engine::shutdown() {
    CrashHandler::logInfo("Engine::shutdown() started");
    
    // Wait for device to finish before cleanup
    vkDeviceWaitIdle(m_vulkanContext->getDevice());
    CrashHandler::logInfo("Device idle, starting cleanup");
    
    // Clean up texture cache
    cleanupTextureCache();
    CrashHandler::logInfo("Texture cache cleaned up");
    
    // Release all thumbnail textures from content items BEFORE destroying thumbnail generator
    // These descriptor sets are allocated from the thumbnail generator's pool
    for (auto& item : m_contentItems) {
        if (item.thumbnailTexture != VK_NULL_HANDLE && m_thumbnailGenerator) {
            m_thumbnailGenerator->releaseThumbnail(item.thumbnailTexture);
            item.thumbnailTexture = VK_NULL_HANDLE;
        }
    }
    CrashHandler::logInfo("Content item thumbnails released");
    
    // Explicitly destroy thumbnail generator before ImGui shutdown
    // This ensures its Vulkan resources are cleaned up while the device is still valid
    m_thumbnailGenerator.reset();
    CrashHandler::logInfo("Thumbnail generator destroyed");
    
    // Now shut down ImGui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    CrashHandler::logInfo("ImGui shutdown complete");
    
    // Explicitly destroy other Vulkan resources in correct order
    m_worldGrid.reset();
    m_gizmoDisc.reset();
    m_gizmoTorus.reset();
    m_gizmoCone.reset();
    m_gizmoCube.reset();
    m_cylinder.reset();
    m_sphere.reset();
    m_cube.reset();
    m_gizmoShader.reset();
    m_maskShader.reset();
    m_offscreenShader.reset();
    m_shader.reset();
    CrashHandler::logInfo("Shaders and meshes destroyed");
    
    // VulkanContext will be destroyed by unique_ptr, then Window
    CrashHandler::logInfo("Engine::shutdown() complete");
}

void Engine::onResize(int width, int height) {
    // Vulkan swap chain will automatically recreate on next beginFrame() call
    // VulkanContext handles swap chain recreation internally
}

void Engine::generateThumbnail(ContentItem& item, Mesh* mesh) {
    if (!m_thumbnailGenerator || !mesh) {
        item.thumbnailTexture = VK_NULL_HANDLE;
        return;
    }
    
    // Release old thumbnail if it exists
    if (item.thumbnailTexture != VK_NULL_HANDLE) {
        m_thumbnailGenerator->releaseThumbnail(item.thumbnailTexture);
    }
    
    // Generate new thumbnail
    item.thumbnailTexture = m_thumbnailGenerator->generateMeshThumbnail(mesh, 128);
}

void Engine::generateMaterialThumbnail(ContentItem& item, const glm::vec3& color) {
    generateMaterialThumbnailWithTexture(item, color, false, "");
}

void Engine::generateMaterialThumbnailWithTexture(ContentItem& item, const glm::vec3& color, bool useTexture, const std::string& texturePath) {
    if (!m_thumbnailGenerator) {
        item.thumbnailTexture = VK_NULL_HANDLE;
        return;
    }
    
    // Wait for device to be idle before modifying Vulkan resources
    // This prevents conflicts with in-flight command buffers
    vkDeviceWaitIdle(m_vulkanContext->getDevice());
    
    // Release old thumbnail if it exists
    if (item.thumbnailTexture != VK_NULL_HANDLE) {
        CrashHandler::logInfo("  Releasing old thumbnail for: " + item.name);
        m_thumbnailGenerator->releaseThumbnail(item.thumbnailTexture);
    }
    
    const uint32_t thumbSize = 128;
    
    // If using a texture, generate thumbnail from the texture file
    if (useTexture && !texturePath.empty()) {
        int width, height, channels;
        unsigned char* pixels = stbi_load(texturePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels) {
            // Resize to thumbnail size and store for persistence
            item.thumbnailWidth = thumbSize;
            item.thumbnailHeight = thumbSize;
            item.thumbnailPixels.resize(thumbSize * thumbSize * 4);
            
            // Simple nearest-neighbor resize
            for (uint32_t y = 0; y < thumbSize; y++) {
                for (uint32_t x = 0; x < thumbSize; x++) {
                    int srcX = static_cast<int>((float)x / thumbSize * width);
                    int srcY = static_cast<int>((float)y / thumbSize * height);
                    srcX = (std::min)(srcX, width - 1);
                    srcY = (std::min)(srcY, height - 1);
                    
                    int srcIndex = (srcY * width + srcX) * 4;
                    int dstIndex = (y * thumbSize + x) * 4;
                    
                    item.thumbnailPixels[dstIndex + 0] = pixels[srcIndex + 0];
                    item.thumbnailPixels[dstIndex + 1] = pixels[srcIndex + 1];
                    item.thumbnailPixels[dstIndex + 2] = pixels[srcIndex + 2];
                    item.thumbnailPixels[dstIndex + 3] = pixels[srcIndex + 3];
                }
            }
            
            stbi_image_free(pixels);
            
            // Create thumbnail from stored pixels
            VkDescriptorSet newThumbnail = m_thumbnailGenerator->createThumbnailFromPixels(
                item.thumbnailPixels.data(), item.thumbnailWidth, item.thumbnailHeight);
            CrashHandler::logInfo("  Generated textured thumbnail for material: " + item.name);
            item.thumbnailTexture = newThumbnail;
            return;
        } else {
            CrashHandler::logError("  Failed to load texture for material thumbnail: " + texturePath);
        }
    }
    
    // Fallback: Generate solid color thumbnail and store pixels for persistence
    item.thumbnailWidth = thumbSize;
    item.thumbnailHeight = thumbSize;
    item.thumbnailPixels.resize(thumbSize * thumbSize * 4);
    
    // Fill with solid color
    uint8_t r = static_cast<uint8_t>(std::clamp(color.r * 255.0f, 0.0f, 255.0f));
    uint8_t g = static_cast<uint8_t>(std::clamp(color.g * 255.0f, 0.0f, 255.0f));
    uint8_t b = static_cast<uint8_t>(std::clamp(color.b * 255.0f, 0.0f, 255.0f));
    
    for (uint32_t i = 0; i < thumbSize * thumbSize; i++) {
        item.thumbnailPixels[i * 4 + 0] = r;
        item.thumbnailPixels[i * 4 + 1] = g;
        item.thumbnailPixels[i * 4 + 2] = b;
        item.thumbnailPixels[i * 4 + 3] = 255;
    }
    
    // Create thumbnail from stored pixels
    VkDescriptorSet newThumbnail = m_thumbnailGenerator->createThumbnailFromPixels(
        item.thumbnailPixels.data(), item.thumbnailWidth, item.thumbnailHeight);
    CrashHandler::logInfo("  Generated color thumbnail for material: " + item.name);
    item.thumbnailTexture = newThumbnail;
}

void Engine::renderSceneTab() {
    // This renders the main viewport - the existing viewport code
    if (m_showViewport) {
        // Make window background transparent so we can see the 3D scene
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("Viewport");
        
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (viewportPanelSize.x != m_viewportWidth || viewportPanelSize.y != m_viewportHeight) {
            m_viewportWidth = static_cast<int>(viewportPanelSize.x);
            m_viewportHeight = static_cast<int>(viewportPanelSize.y);
        }
        
        // Store viewport position and size for mouse interaction
        ImVec2 viewportPos = ImGui::GetCursorScreenPos();
        m_viewportPos = viewportPos;
        m_viewportSize = viewportPanelSize;
        
        // Display the composite output (scene with selection outline)
        ImTextureID texId = (ImTextureID)m_vulkanContext->getCompositeDescriptorSet();
        
        // Display the selected texture
        ImGui::Image(texId, viewportPanelSize);
        
        // Track if we're currently dragging over the viewport
        bool isDraggingOverViewport = false;
        glm::vec3 currentDragPosition(0.0f);
        
        // Drag and drop target for content browser items
        if (ImGui::BeginDragDropTarget()) {
            // Check if we're dragging a content item
            if (const ImGuiPayload* payload = ImGui::GetDragDropPayload()) {
                if (payload->IsDataType("CONTENT_ITEM")) {
                    size_t itemIndex = *(const size_t*)payload->Data;
                    if (itemIndex < m_contentItems.size()) {
                        ContentItem& item = m_contentItems[itemIndex];
                        
                        // Only track Objects
                        if (item.type == "Object") {
                            // Calculate position for preview
                            ImVec2 mousePos = ImGui::GetMousePos();
                            float mouseX = mousePos.x - m_viewportPos.x;
                            float mouseY = mousePos.y - m_viewportPos.y;
                            
                            // Convert to NDC
                            float ndcX = (2.0f * mouseX) / m_viewportSize.x - 1.0f;
                            float ndcY = 1.0f - (2.0f * mouseY) / m_viewportSize.y;
                            
                            // Create ray
                            glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
                            glm::mat4 projection = m_camera->getProjectionMatrix((float)m_viewportWidth / (float)m_viewportHeight);
                            glm::vec4 rayEye = glm::inverse(projection) * rayClip;
                            rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
                            
                            glm::mat4 view = m_camera->getViewMatrix();
                            glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
                            rayWorld = glm::normalize(rayWorld);
                            glm::vec3 rayOrigin = m_camera->getPosition();
                            
                            // Intersect ray with Z=0 plane
                            glm::vec3 planeNormal(0.0f, 0.0f, 1.0f);
                            float denom = glm::dot(rayWorld, planeNormal);
                            if (abs(denom) > 0.0001f) {
                                float t = glm::dot(glm::vec3(0.0f) - rayOrigin, planeNormal) / denom;
                                if (t >= 0) {
                                    currentDragPosition = rayOrigin + rayWorld * t;
                                    isDraggingOverViewport = true;
                                    m_isDraggingObject = true;
                                    m_draggedObjectIndex = static_cast<int>(itemIndex);
                                    m_dragPreviewPosition = currentDragPosition;
                                }
                            }
                        }
                    }
                }
            }
            
            // Handle drop
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ITEM")) {
                size_t itemIndex = *(const size_t*)payload->Data;
                if (itemIndex < m_contentItems.size()) {
                    ContentItem& item = m_contentItems[itemIndex];
                    
                    // Only allow Objects to be dragged into the scene
                    if (item.type == "Object") {
                        // Get mouse position in viewport
                        ImVec2 mousePos = ImGui::GetMousePos();
                        float mouseX = mousePos.x - m_viewportPos.x;
                        float mouseY = mousePos.y - m_viewportPos.y;
                        
                        // Convert to NDC
                        float ndcX = (2.0f * mouseX) / m_viewportSize.x - 1.0f;
                        float ndcY = 1.0f - (2.0f * mouseY) / m_viewportSize.y;
                        
                        // Create ray
                        glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
                        glm::mat4 projection = m_camera->getProjectionMatrix((float)m_viewportWidth / (float)m_viewportHeight);
                        glm::vec4 rayEye = glm::inverse(projection) * rayClip;
                        rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
                        
                        glm::mat4 view = m_camera->getViewMatrix();
                        glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
                        rayWorld = glm::normalize(rayWorld);
                        glm::vec3 rayOrigin = m_camera->getPosition();
                        
                        // Intersect ray with Z=0 plane
                        glm::vec3 planeNormal(0.0f, 0.0f, 1.0f);
                        float denom = glm::dot(rayWorld, planeNormal);
                        if (abs(denom) > 0.0001f) {
                            float t = glm::dot(glm::vec3(0.0f) - rayOrigin, planeNormal) / denom;
                            if (t >= 0) {
                                glm::vec3 spawnPos = rayOrigin + rayWorld * t;
                                
                                // Use the Object's name as base name (not mesh name)
                                std::string baseName = item.name;
                                
                                int counter = 1;
                                std::string objectName;
                                bool nameExists;
                                do {
                                    // Use Object name with counter
                                    objectName = baseName + "_" + std::string(counter < 10 ? "0" : "") + std::to_string(counter);
                                    nameExists = false;
                                    for (const auto& obj : m_sceneObjects) {
                                        if (obj.name == objectName) {
                                            nameExists = true;
                                            counter++;
                                            break;
                                        }
                                    }
                                } while (nameExists);
                                
                                // Add object to scene
                                SceneObject newObj(objectName);
                                newObj.position = spawnPos;
                                newObj.sourceObjectIndex = static_cast<int>(itemIndex);  // Track which Object this came from
                                newObj.sourceObjectGUID = item.id;  // Store source object GUID
                                
                                // Store mesh/material by GUID (stable references)
                                // A new instance overrides nothing: it takes the asset's mesh and material
                                // (and keeps following them if they change).
                                applyAssetDefaults(newObj);

                                m_sceneObjects.push_back(newObj);
                                m_selectedObjectIndex = static_cast<int>(m_sceneObjects.size() - 1);
                                
                                // Push undo state
                                pushUndoState();
                                
                                // Mark project as modified
                                markProjectModified();
                                
                                m_consoleBuffer.push_back("Created object: " + objectName + " at (" + 
                                    std::to_string(spawnPos.x) + ", " + std::to_string(spawnPos.y) + ", " + std::to_string(spawnPos.z) + ")");
                            }
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        
        // Clear drag state if not dragging
        if (!isDraggingOverViewport) {
            m_isDraggingObject = false;
            m_draggedObjectIndex = -1;
        }
        
        // Track viewport state for mouse picking
        m_viewportHovered = ImGui::IsItemHovered();
        m_viewportFocused = ImGui::IsWindowFocused();
        m_viewportPos = ImGui::GetItemRectMin();
        m_viewportSize = ImGui::GetItemRectSize();
        
        // Check for click in viewport BEFORE rendering buttons
        // This way button clicks won't trigger viewport selection
        bool viewportClicked = !m_isPlaying && m_viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        
        // Handle gizmo drag
        if (m_gizmoActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            handleGizmoInteraction();
            
            // Log rotation frame if actively rotating
            if (m_gizmoMode == GizmoMode::Rotate && m_isLoggingRotation) {
                logRotationFrame();
            }
        }
        
        // End gizmo drag
        if (m_gizmoActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_gizmoActive = false;
            m_activeGizmoAxis = GizmoAxis::None;
            
            // Clear rotation feedback
            m_rotationAccumulatedAngle = 0.0f;
            m_rotationStartAngle = 0.0f;
            m_rotationCurrentAngle = 0.0f;
            m_rotationIsPositive = true;
            
            // Mark project as modified when finishing a transformation
            markProjectModified();
            
            // End rotation logging if was rotating
            if (m_isLoggingRotation) {
                endRotationLog();
            }
        }
        
        // Update hover state when not dragging
        if (!m_gizmoActive && m_viewportHovered && m_selectedObjectIndex >= 0 && m_gizmoMode != GizmoMode::None) {
            ImVec2 mousePos = ImGui::GetMousePos();
            float mouseX = mousePos.x - m_viewportPos.x;
            float mouseY = mousePos.y - m_viewportPos.y;
            
            float ndcX = (2.0f * mouseX) / m_viewportSize.x - 1.0f;
            float ndcY = 1.0f - (2.0f * mouseY) / m_viewportSize.y;
            
            glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
            // Use unflipped projection for ray casting (same as InputHandler.cpp)
            float aspectRatio = m_viewportSize.x / m_viewportSize.y;
            glm::mat4 projection = m_camera->getProjectionMatrixForRayCast(aspectRatio);
            glm::vec4 rayEye = glm::inverse(projection) * rayClip;
            rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
            
            glm::mat4 view = m_camera->getViewMatrix();
            glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
            rayWorld = glm::normalize(rayWorld);
            glm::vec3 rayOrigin = m_camera->getPosition();
            
            GizmoAxis hoveredAxis;
            if (rayCastGizmo(rayOrigin, rayWorld, hoveredAxis)) {
                m_hoveredGizmoAxis = hoveredAxis;
            } else {
                m_hoveredGizmoAxis = GizmoAxis::None;
            }
        } else if (m_gizmoActive) {
            // Keep hover state while dragging
            m_hoveredGizmoAxis = m_activeGizmoAxis;
        } else {
            m_hoveredGizmoAxis = GizmoAxis::None;
        }
        
        // Render tool buttons overlay if object is selected
        if (m_selectedObjectIndex >= 0) {
            renderToolButtons();
        }
        
        // DEBUG: Always log click attempts
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            char debugMsg[256];
            snprintf(debugMsg, sizeof(debugMsg), "LEFT CLICK: viewportHovered=%d, viewportClicked=%d, anyItemHovered=%d",
                     m_viewportHovered ? 1 : 0, viewportClicked ? 1 : 0, ImGui::IsAnyItemHovered() ? 1 : 0);
            m_consoleBuffer.push_back(debugMsg);
        }
        
        // Process viewport click only if we actually clicked and not on a button
        if (viewportClicked && !ImGui::IsAnyItemHovered()) {
            checkViewportClick();
        }
        
        // Debug: Log when we click in viewport but don't process
        if (viewportClicked && ImGui::IsAnyItemHovered()) {
            m_consoleBuffer.push_back("Viewport click blocked - item hovered");
        }
        
        if (viewportClicked && !m_viewportHovered) {
            m_consoleBuffer.push_back("Viewport click blocked - not hovered");
        }
        
        // FPS display overlay (bottom-right corner)
        if (m_showFPS) {
            ImVec2 windowPos = ImGui::GetWindowPos();
            ImVec2 windowSize = ImGui::GetWindowSize();
            float fps = ImGui::GetIO().Framerate;
            
            // Format FPS text
            char fpsText[32];
            snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", fps);
            
            // Calculate text size
            ImVec2 textSize = ImGui::CalcTextSize(fpsText);
            float padding = 20.0f;
            
            // Position at bottom-right with padding
            ImVec2 textPos(
                windowPos.x + windowSize.x - textSize.x - padding,
                windowPos.y + windowSize.y - textSize.y - padding
            );
            
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            // Draw drop shadow (offset by 1 pixel)
            drawList->AddText(
                ImVec2(textPos.x + 1, textPos.y + 1),
                IM_COL32(0, 0, 0, 128),  // Black with 50% alpha
                fpsText
            );
            
            // Draw main text in white
            drawList->AddText(
                textPos,
                IM_COL32(255, 255, 255, 255),  // White
                fpsText
            );
        }
        
        // Statistics overlay (top-left corner)
        if (m_showStatistics) {
            ImVec2 windowPos = ImGui::GetWindowPos();
            float padding = 20.0f;
            
            // Get camera data
            glm::vec3 camPos = m_camera->getPosition();
            float yaw = m_camera->getYaw();
            float pitch = m_camera->getPitch();
            
            // Format statistics text
            char statsText[256];
            snprintf(statsText, sizeof(statsText), 
                "Camera Position: (%.2f, %.2f, %.2f)\n"
                "Camera Rotation: Yaw=%.2f, Pitch=%.2f",
                camPos.x, camPos.y, camPos.z,
                yaw, pitch
            );
            
            ImVec2 textPos(windowPos.x + padding, windowPos.y + padding);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            // Draw drop shadow
            drawList->AddText(
                ImVec2(textPos.x + 1, textPos.y + 1),
                IM_COL32(0, 0, 0, 128),
                statsText
            );
            
            // Draw main text
            drawList->AddText(
                textPos,
                IM_COL32(255, 255, 255, 255),
                statsText
            );
        }
        
        ImGui::End();
        ImGui::PopStyleColor(2); // Pop WindowBg and ChildBg
    }
}

void Engine::saveProject(const std::string& filepath) {
    stopPlay();  // never save the scene while scripts have it moved around
    ProjectData data;
    
    // Window state (get from GLFW)
    int width, height;
    glfwGetWindowSize(m_window->getNativeWindow(), &width, &height);
    data.windowWidth = width;
    data.windowHeight = height;
    data.windowMaximized = (glfwGetWindowAttrib(m_window->getNativeWindow(), GLFW_MAXIMIZED) == GLFW_TRUE);
    
    // Camera state
    data.cameraPosition = m_camera->getPosition();
    data.cameraFront = m_camera->getFront();
    data.cameraUp = m_camera->getUp();
    data.cameraYaw = m_camera->getYaw();
    data.cameraPitch = m_camera->getPitch();
    
    // Scene data
    data.sceneObjects = m_sceneObjects;
    data.selectedObjectIndex = m_selectedObjectIndex;
    
    // Content items
    data.contentItems = m_contentItems;
    data.selectedContentIndex = m_selectedContentIndex;
    
    // Folders
    data.folders = m_folders;
    data.currentFolder = m_currentFolder;
    data.selectedFolderIndex = m_selectedFolderIndex;
    
    // Tabs
    data.tabs = m_tabs;
    data.activeTabIndex = m_activeTabIndex;
    // A script graph tab isn't kept in the project: it is opened from an Object's Script section.
    {
        int removedBeforeActive = 0;
        for (int i = static_cast<int>(data.tabs.size()) - 1; i >= 0; i--) {
            if (data.tabs[i].type == TabType::ScriptGraph) {
                if (i <= data.activeTabIndex) removedBeforeActive++;
                data.tabs.erase(data.tabs.begin() + i);
            }
        }
        data.activeTabIndex = (std::max)(0, data.activeTabIndex - removedBeforeActive);
    }
    
    // UI state
    data.showObjectsPanel = m_showObjectsPanel;
    data.showContentBrowser = m_showContentBrowser;
    
    if (ProjectSerializer::saveProject(filepath, data)) {
        m_currentProjectPath = filepath;
        m_projectModified = false;
        
        // Clear unsaved changes flag on all tabs
        for (auto& tab : m_tabs) {
            tab.hasUnsavedChanges = false;
        }
        
        std::cout << "Project saved successfully!" << std::endl;
    } else {
        std::cerr << "Failed to save project!" << std::endl;
    }
}

void Engine::saveAllUnsavedAssets() {
    if (m_currentProjectPath.empty()) {
        CrashHandler::logWarning("Cannot save assets: no project path set");
        return;
    }
    
    std::filesystem::path projectFile(m_currentProjectPath);
    std::filesystem::path projectDir = projectFile.parent_path();
    
    // Make a copy of the set since we'll be modifying it during iteration
    std::set<int> itemsToSave = m_unsavedContentItems;
    
    for (int itemIndex : itemsToSave) {
        if (itemIndex < 0 || itemIndex >= static_cast<int>(m_contentItems.size())) {
            continue;
        }
        
        ContentItem& item = m_contentItems[itemIndex];
        
        if (item.type == "Material" && !item.filepath.empty()) {
            try {
                std::filesystem::path materialFile = projectDir / item.filepath;
                std::filesystem::create_directories(materialFile.parent_path());
                
                // Check if there's an open tab for this item to get full node data
                for (auto& tab : m_tabs) {
                    if (tab.contentItemIndex == itemIndex && tab.nodeGraph) {
                        // Update full node data from the open tab's node graph
                        item.nodes.clear();
                        item.connections.clear();
                        
                        // Save all nodes with their full data
                        for (const auto& node : tab.nodeGraph->getNodes()) {
                            MaterialNodeData nodeData;
                            nodeData.nodeType = node->type;
                            nodeData.nodeId = node->id;
                            nodeData.posX = node->position.x;
                            nodeData.posY = node->position.y;
                            
                            // Check if it's a ColorWheel node and save its color data
                            if (node->type == "ColorWheel") {
                                ColorWheelNode* colorNode = dynamic_cast<ColorWheelNode*>(node.get());
                                if (colorNode) {
                                    nodeData.color = colorNode->color;
                                    nodeData.hue = colorNode->hue;
                                    nodeData.saturation = colorNode->saturation;
                                    nodeData.value = colorNode->value;
                                }
                            }
                            // Save Image node-specific properties
                            else if (node->type == "Image") {
                                ImageNode* imageNode = dynamic_cast<ImageNode*>(node.get());
                                if (imageNode) {
                                    nodeData.texturePath = imageNode->selectedTexturePath;
                                    nodeData.textureType = static_cast<int>(imageNode->textureType);
                                    nodeData.strength = imageNode->strength;
                                    nodeData.invertTexture = imageNode->invertTexture;
                                }
                            }
                            
                            item.nodes.push_back(nodeData);
                        }
                        
                        // Save all connections
                        for (const auto& conn : tab.nodeGraph->getConnections()) {
                            item.connections.emplace_back(
                                conn.outputNodeId,
                                conn.outputPinId,
                                conn.inputNodeId,
                                conn.inputPinId
                            );
                        }
                        
                        tab.hasUnsavedChanges = false;
                        if (tab.nodeGraph) {
                            tab.nodeGraph->clearUnsavedChanges();
                        }
                        break;
                    }
                }
                
                // Evaluate material and generate thumbnail FIRST (before writing file)
                glm::vec3 evaluatedColor = item.materialColor;
                bool useAlbedoTexture = false;
                std::string albedoTexturePath;
                
                for (auto& tab : m_tabs) {
                    if (tab.contentItemIndex == itemIndex && tab.nodeGraph) {
                        EvaluatedMaterial evalMat = tab.nodeGraph->evaluateMaterial();
                        evaluatedColor = evalMat.baseColor;
                        useAlbedoTexture = evalMat.useAlbedoTexture;
                        albedoTexturePath = evalMat.albedoTexturePath;
                        break;
                    }
                }
                
                item.materialColor = evaluatedColor;
                
                // Generate thumbnail (populates item.thumbnailPixels for file)
                generateMaterialThumbnailWithTexture(item, evaluatedColor, useAlbedoTexture, albedoTexturePath);
                
                        std::ofstream matFile(materialFile, std::ios::binary);
                        if (matFile.is_open()) {
                            // Write header
                            matFile.write("MAT", 3);
                            int formatMarker = 1;
                            matFile.write(reinterpret_cast<const char*>(&formatMarker), sizeof(int));
                            
                            // Write GUID
                            size_t guidLength = item.id.length();
                    matFile.write(reinterpret_cast<const char*>(&guidLength), sizeof(size_t));
                    matFile.write(item.id.c_str(), guidLength);
                    
                    // Write color
                    matFile.write(reinterpret_cast<const char*>(&item.materialColor.r), sizeof(float));
                    matFile.write(reinterpret_cast<const char*>(&item.materialColor.g), sizeof(float));
                    matFile.write(reinterpret_cast<const char*>(&item.materialColor.b), sizeof(float));
                    
                    // Write full node data
                    size_t nodeCount = item.nodes.size();
                    matFile.write(reinterpret_cast<const char*>(&nodeCount), sizeof(size_t));
                    for (const auto& nodeData : item.nodes) {
                        // Node type
                        size_t typeLength = nodeData.nodeType.length();
                        matFile.write(reinterpret_cast<const char*>(&typeLength), sizeof(size_t));
                        matFile.write(nodeData.nodeType.c_str(), typeLength);
                        
                        // Basic position data
                        matFile.write(reinterpret_cast<const char*>(&nodeData.nodeId), sizeof(int));
                        matFile.write(reinterpret_cast<const char*>(&nodeData.posX), sizeof(float));
                        matFile.write(reinterpret_cast<const char*>(&nodeData.posY), sizeof(float));
                        
                        // ColorWheel-specific data (always write for consistency)
                        matFile.write(reinterpret_cast<const char*>(&nodeData.color.r), sizeof(float));
                        matFile.write(reinterpret_cast<const char*>(&nodeData.color.g), sizeof(float));
                        matFile.write(reinterpret_cast<const char*>(&nodeData.color.b), sizeof(float));
                        matFile.write(reinterpret_cast<const char*>(&nodeData.hue), sizeof(float));
                        matFile.write(reinterpret_cast<const char*>(&nodeData.saturation), sizeof(float));
                        matFile.write(reinterpret_cast<const char*>(&nodeData.value), sizeof(float));
                        
                        // Image node-specific data (v4)
                        size_t pathLength = nodeData.texturePath.length();
                        matFile.write(reinterpret_cast<const char*>(&pathLength), sizeof(size_t));
                        if (pathLength > 0) {
                            matFile.write(nodeData.texturePath.c_str(), pathLength);
                        }
                        matFile.write(reinterpret_cast<const char*>(&nodeData.textureType), sizeof(int));
                        
                        // Texture strength (v6, was normalStrength)
                        matFile.write(reinterpret_cast<const char*>(&nodeData.strength), sizeof(float));
                        
                        // Invert texture flag (v7)
                        uint8_t invertFlag = nodeData.invertTexture ? 1 : 0;
                        matFile.write(reinterpret_cast<const char*>(&invertFlag), sizeof(uint8_t));
                    }
                    
                    // Write connections
                    size_t connCount = item.connections.size();
                    matFile.write(reinterpret_cast<const char*>(&connCount), sizeof(size_t));
                    for (const auto& conn : item.connections) {
                        matFile.write(reinterpret_cast<const char*>(&conn.outputNodeId), sizeof(int));
                        matFile.write(reinterpret_cast<const char*>(&conn.outputPinId), sizeof(int));
                        matFile.write(reinterpret_cast<const char*>(&conn.inputNodeId), sizeof(int));
                        matFile.write(reinterpret_cast<const char*>(&conn.inputPinId), sizeof(int));
                    }
                    
                    // Write thumbnail data (v5)
                    matFile.write(reinterpret_cast<const char*>(&item.thumbnailWidth), sizeof(uint32_t));
                    matFile.write(reinterpret_cast<const char*>(&item.thumbnailHeight), sizeof(uint32_t));
                    size_t pixelDataSize = item.thumbnailPixels.size();
                    matFile.write(reinterpret_cast<const char*>(&pixelDataSize), sizeof(size_t));
                    if (pixelDataSize > 0) {
                        matFile.write(reinterpret_cast<const char*>(item.thumbnailPixels.data()), pixelDataSize);
                    }
                    
                    // Write splitter ratio
                    matFile.write(reinterpret_cast<const char*>(&item.splitterRatio), sizeof(float));
                    
                    matFile.close();
                    CrashHandler::logInfo("Saved material (v5): " + item.name + " with thumbnail");
                    
                    // Update scene objects using this material
                    for (auto& obj : m_sceneObjects) {
                        if (objectUsesMaterial(obj, static_cast<int>(itemIndex))) {
                            obj.color = evaluatedColor;
                        }
                    }
                } else {
                    CrashHandler::logError("Failed to save material: " + materialFile.string());
                }
            } catch (const std::exception& e) {
                CrashHandler::logError("Exception saving material: " + std::string(e.what()));
            }
        }
    }
    
    // Clear the unsaved items set
    m_unsavedContentItems.clear();
    m_projectModified = false;
}

void Engine::loadProject(const std::string& filepath) {
    stopPlay();
    ProjectData data;
    
    if (!ProjectSerializer::loadProject(filepath, data)) {
        std::cerr << "Failed to load project!" << std::endl;
        return;
    }
    
    // Clear existing data
    m_sceneObjects.clear();
    m_contentItems.clear();
    
    // Cleanup existing tab framebuffers
    for (auto& tab : m_tabs) {
        // TODO: Cleanup Vulkan framebuffer resources
    }
    m_tabs.clear();
    
    // Apply window state
    glfwSetWindowSize(m_window->getNativeWindow(), data.windowWidth, data.windowHeight);
    if (data.windowMaximized) {
        glfwMaximizeWindow(m_window->getNativeWindow());
    }
    
    // Apply camera state
    m_camera->setPosition(data.cameraPosition);
    m_camera->setYaw(data.cameraYaw);
    m_camera->setPitch(data.cameraPitch);
    m_camera->updateCameraVectors();
    
    // Load scene objects
    m_sceneObjects = data.sceneObjects;
    m_selectedObjectIndex = data.selectedObjectIndex;
    
    // Add default cube if scene is empty
    if (m_sceneObjects.empty()) {
        m_sceneObjects.push_back(SceneObject("Cube_01"));
        CrashHandler::logInfo("Added default cube to empty scene");
    }
    
    // Content items will be loaded from filesystem via scanAssetsDirectory()
    // No longer loading from project file
    
    // Load tabs and recreate their framebuffers
    m_tabs = data.tabs;
    m_activeTabIndex = data.activeTabIndex;
    
    for (auto& tab : m_tabs) {
        // TODO: Recreate Vulkan framebuffer resources for tab
    }
    
    // Load UI state
    m_showObjectsPanel = data.showObjectsPanel;
    m_showContentBrowser = data.showContentBrowser;
    
    // Update project state
    m_currentProjectPath = filepath;
    m_projectModified = false;
    m_unsavedContentItems.clear();
    m_selectedObjectIndex = -1; // Clear selection on load
    
    // Scan filesystem to populate content browser with all folders
    scanAssetsDirectory(true);

    // Placed objects follow their Object asset's defaults except where they override them. A project
    // saved before overrides were recorded: whatever differs from the asset counts as an override.
    for (SceneObject& obj : m_sceneObjects) {
        if (data.fileVersion < 3) inferOverrides(obj);
        applyAssetDefaults(obj);
    }

    // Reset docking layout to refresh UI
    m_dockingLayoutInitialized = false;
    
    std::cout << "Project loaded successfully!" << std::endl;
}

void Engine::newProject() {
    stopPlay();
    // Clear all data
    m_sceneObjects.clear();
    m_selectedObjectIndex = -1;
    
    // Scene starts empty - user can drag objects from Content Browser
    CrashHandler::logInfo("Created new project with empty scene");
    
    // Clear content items (keep default meshes if any)
    // TODO: Implement Vulkan texture deletion
    for (auto& item : m_contentItems) {
        // if (item.thumbnailTexture) {
        //     Delete Vulkan image/imageView/memory here
        // }
    }
    m_contentItems.clear();
    m_selectedContentIndex = -1;
    
    // Setup default folder structure
    m_folders.clear();
    m_folders.push_back("Meshes");
    m_folders.push_back("Materials");
    m_folders.push_back("Textures");
    m_folders.push_back("Objects");
    std::sort(m_folders.begin(), m_folders.end());  // Sort alphabetically
    m_currentFolder = "Meshes";  // Start in Meshes folder
    
    // Find the index of "Meshes" after sorting
    auto it = std::find(m_folders.begin(), m_folders.end(), "Meshes");
    m_selectedFolderIndex = (it != m_folders.end()) ? static_cast<int>(std::distance(m_folders.begin(), it)) : 0;
    
    // Create default meshes in Meshes folder
    ContentItem cube("Cube", "Mesh", "Meshes", "Assets/Meshes/Cube.fbx");
    m_contentItems.push_back(cube);
    generateThumbnail(m_contentItems.back(), m_cube.get());
    
    ContentItem sphere("Sphere", "Mesh", "Meshes", "Assets/Meshes/Sphere.fbx");
    m_contentItems.push_back(sphere);
    generateThumbnail(m_contentItems.back(), m_sphere.get());
    
    ContentItem cylinder("Cylinder", "Mesh", "Meshes", "Assets/Meshes/Cylinder.fbx");
    m_contentItems.push_back(cylinder);
    generateThumbnail(m_contentItems.back(), m_cylinder.get());
    
    // Close all tabs except Scene
    for (size_t i = m_tabs.size(); i > 1; i--) {
        auto& tab = m_tabs[i - 1];
        // TODO: Cleanup Vulkan framebuffer resources
    }
    m_tabs.erase(m_tabs.begin() + 1, m_tabs.end());
    m_activeTabIndex = 0;
    
    // Reset camera
    m_camera->setPosition(glm::vec3(7.0f, 7.0f, 5.0f));
    m_camera->setYaw(-135.0f);
    m_camera->setPitch(-30.0f);
    m_camera->updateCameraVectors();
    
    // Reset project state
    m_currentProjectPath.clear();
    m_projectModified = false;
    m_unsavedContentItems.clear();
    
    // Clear undo/redo stacks
    m_undoStack.clear();
    m_redoStack.clear();
    
    std::cout << "New project created" << std::endl;
}

void Engine::scanAssetsDirectory(bool scanAllFolders) {
    if (m_currentProjectPath.empty()) {
        return; // No project loaded
    }
    
    CrashHandler::logInfo(scanAllFolders ? "Scanning ALL assets from filesystem" : "Scanning assets directory from filesystem");
    
    // Get project directory
    std::filesystem::path projectFile(m_currentProjectPath);
    std::filesystem::path projectDir = projectFile.parent_path();
    std::filesystem::path assetsDir = projectDir / "Assets";
    
    // Clear existing content
    // TODO: Implement Vulkan texture deletion
    for (auto& item : m_contentItems) {
        // if (item.thumbnailTexture) {
        //     Delete Vulkan image/imageView/memory here
        // }
    }
    m_contentItems.clear();
    
    // Only clear folders if scanning all folders
    if (scanAllFolders) {
        m_folders.clear();
    }
    
    // Check if Assets directory exists
    if (!std::filesystem::exists(assetsDir)) {
        CrashHandler::logWarning("Assets directory does not exist, creating default structure");
        std::filesystem::create_directories(assetsDir / "Meshes");
        std::filesystem::create_directories(assetsDir / "Materials");
        std::filesystem::create_directories(assetsDir / "Textures");
        std::filesystem::create_directories(assetsDir / "Objects");
    }
    
    // Ensure default meshes are copied to project's Meshes folder
    std::filesystem::path meshesDir = assetsDir / "Meshes";
    if (!std::filesystem::exists(meshesDir)) {
        std::filesystem::create_directories(meshesDir);
    }
    
    // Copy default meshes from resources/default_meshes if they don't exist in project
    std::filesystem::path defaultMeshesDir = "resources/default_meshes";
    if (std::filesystem::exists(defaultMeshesDir)) {
        std::vector<std::string> defaultMeshes = {"Cube.fbx", "Sphere.fbx", "Cylinder.fbx"};
        for (const auto& meshName : defaultMeshes) {
            std::filesystem::path sourcePath = defaultMeshesDir / meshName;
            std::filesystem::path destPath = meshesDir / meshName;
            if (std::filesystem::exists(sourcePath) && !std::filesystem::exists(destPath)) {
                try {
                    std::filesystem::copy_file(sourcePath, destPath);
                    CrashHandler::logInfo("Copied default mesh to project: " + meshName);
                } catch (const std::exception& e) {
                    CrashHandler::logWarning("Failed to copy default mesh " + meshName + ": " + e.what());
                }
            }
        }
    }
    
    // Scan for folders in Assets directory (only if scanning all)
    if (scanAllFolders) {
        for (const auto& entry : std::filesystem::directory_iterator(assetsDir)) {
            if (entry.is_directory()) {
                std::string folderName = entry.path().filename().string();
                m_folders.push_back(folderName);
            }
        }
        
        // Sort folders alphabetically
        std::sort(m_folders.begin(), m_folders.end());
        
        // Ensure current folder exists, default to first folder or Meshes
        if (m_folders.empty()) {
            m_folders.push_back("Meshes");
            m_currentFolder = "Meshes";
            m_selectedFolderIndex = 0;
            std::filesystem::create_directories(assetsDir / "Meshes");
        } else {
            auto it = std::find(m_folders.begin(), m_folders.end(), m_currentFolder);
            if (it == m_folders.end()) {
                m_currentFolder = m_folders[0];
                m_selectedFolderIndex = 0;
            } else {
                m_selectedFolderIndex = static_cast<int>(std::distance(m_folders.begin(), it));
            }
        }
    }
    
    // Determine which folders to scan
    std::vector<std::string> foldersToScan;
    if (scanAllFolders) {
        foldersToScan = m_folders; // Scan all folders
    } else {
        foldersToScan.push_back(m_currentFolder); // Scan only current folder
    }
    
    // Scan files in selected folder(s)
    for (const auto& folderName : foldersToScan) {
        std::filesystem::path folderPath = assetsDir / folderName;
        if (!std::filesystem::exists(folderPath)) {
            continue;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                std::string extension = entry.path().extension().string();
                std::string nameWithoutExt = entry.path().stem().string();
                
                // Get file creation time
                time_t creationDate = std::filesystem::last_write_time(entry).time_since_epoch().count();
                
                // Determine type and create ContentItem
                std::string relativePathStr = "Assets\\" + folderName + "\\" + filename;
                
                if (extension == ".fbx" || extension == ".obj" || extension == ".FBX" || extension == ".OBJ") {
                    // Mesh file
                    ContentItem item(nameWithoutExt, "Mesh", folderName, relativePathStr);
                    item.creationDate = creationDate;
                    m_contentItems.push_back(item);
                    
                    // Generate thumbnail based on mesh name
                    Mesh* mesh = nullptr;
                    if (nameWithoutExt.find("Sphere") != std::string::npos) {
                        mesh = m_sphere.get();
                    } else if (nameWithoutExt.find("Cylinder") != std::string::npos) {
                        mesh = m_cylinder.get();
                    } else if (nameWithoutExt.find("Cube") != std::string::npos) {
                        mesh = m_cube.get();
                    } else {
                        mesh = m_cube.get(); // Default to cube for unknown meshes
                    }
                    generateThumbnail(m_contentItems.back(), mesh);
                    
                } else if (extension == ".fmat") {
                    // Material file - load material data
                    ContentItem item(nameWithoutExt, "Material", folderName, relativePathStr);
                    item.creationDate = creationDate;
                    item.materialColor = glm::vec3(0.7f, 0.7f, 0.7f); // Default gray
                    
                    // Read material data from file
                    std::ifstream matFile(entry.path(), std::ios::binary);
                    if (matFile.is_open()) {
                        char matHeader[3];
                        matFile.read(matHeader, 3);
                        if (std::string(matHeader, 3) == "MAT") {
                            // Read format marker
                            int formatMarker;
                            matFile.read(reinterpret_cast<char*>(&formatMarker), sizeof(int));
                            
                            // Read GUID
                            size_t guidLength;
                            matFile.read(reinterpret_cast<char*>(&guidLength), sizeof(size_t));
                            
                            // Sanity check GUID length
                            if (guidLength == 0 || guidLength > 1000) {
                                CrashHandler::logWarning("Material file '" + nameWithoutExt + "' has invalid GUID length, skipping");
                                matFile.close();
                                item.id = ForgeEngine::Utils::generateGUID();
                                m_contentItems.push_back(item);
                                generateMaterialThumbnail(m_contentItems.back(), m_contentItems.back().materialColor);
                                continue;
                            }
                            
                            std::string guid(guidLength, '\0');
                            matFile.read(&guid[0], guidLength);
                            item.id = guid;
                            
                            // Read color
                            matFile.read(reinterpret_cast<char*>(&item.materialColor.r), sizeof(float));
                            matFile.read(reinterpret_cast<char*>(&item.materialColor.g), sizeof(float));
                            matFile.read(reinterpret_cast<char*>(&item.materialColor.b), sizeof(float));
                            
                            // Read node data
                            size_t nodeCount;
                            matFile.read(reinterpret_cast<char*>(&nodeCount), sizeof(size_t));
                            
                            // Sanity check node count
                            if (nodeCount > 10000) {
                                CrashHandler::logWarning("Material file '" + nameWithoutExt + "' has invalid node count, skipping nodes");
                                nodeCount = 0;
                            }
                            
                            for (size_t i = 0; i < nodeCount && matFile.good(); i++) {
                                MaterialNodeData nodeData;
                                
                                // Node type
                                size_t typeLength;
                                matFile.read(reinterpret_cast<char*>(&typeLength), sizeof(size_t));
                                if (typeLength > 1000) break;  // Sanity check
                                nodeData.nodeType.resize(typeLength);
                                matFile.read(&nodeData.nodeType[0], typeLength);
                                
                                // Basic position data
                                matFile.read(reinterpret_cast<char*>(&nodeData.nodeId), sizeof(int));
                                matFile.read(reinterpret_cast<char*>(&nodeData.posX), sizeof(float));
                                matFile.read(reinterpret_cast<char*>(&nodeData.posY), sizeof(float));
                                
                                // ColorWheel-specific data
                                matFile.read(reinterpret_cast<char*>(&nodeData.color.r), sizeof(float));
                                matFile.read(reinterpret_cast<char*>(&nodeData.color.g), sizeof(float));
                                matFile.read(reinterpret_cast<char*>(&nodeData.color.b), sizeof(float));
                                matFile.read(reinterpret_cast<char*>(&nodeData.hue), sizeof(float));
                                matFile.read(reinterpret_cast<char*>(&nodeData.saturation), sizeof(float));
                                matFile.read(reinterpret_cast<char*>(&nodeData.value), sizeof(float));
                                
                                // Image node-specific data
                                size_t pathLength;
                                matFile.read(reinterpret_cast<char*>(&pathLength), sizeof(size_t));
                                if (pathLength > 10000) break;  // Sanity check
                                if (pathLength > 0) {
                                    nodeData.texturePath.resize(pathLength);
                                    matFile.read(&nodeData.texturePath[0], pathLength);
                                }
                                matFile.read(reinterpret_cast<char*>(&nodeData.textureType), sizeof(int));
                                
                                // Texture strength
                                matFile.read(reinterpret_cast<char*>(&nodeData.strength), sizeof(float));
                                
                                // Invert texture flag
                                uint8_t invertFlag;
                                matFile.read(reinterpret_cast<char*>(&invertFlag), sizeof(uint8_t));
                                nodeData.invertTexture = (invertFlag != 0);
                                
                                item.nodes.push_back(nodeData);
                            }
                            
                            // Read connections
                            size_t connCount = 0;
                            if (matFile.good()) {
                                matFile.read(reinterpret_cast<char*>(&connCount), sizeof(size_t));
                                if (connCount > 10000) connCount = 0;  // Sanity check
                            }
                            for (size_t i = 0; i < connCount && matFile.good(); i++) {
                                MaterialConnection conn;
                                matFile.read(reinterpret_cast<char*>(&conn.outputNodeId), sizeof(int));
                                matFile.read(reinterpret_cast<char*>(&conn.outputPinId), sizeof(int));
                                matFile.read(reinterpret_cast<char*>(&conn.inputNodeId), sizeof(int));
                                matFile.read(reinterpret_cast<char*>(&conn.inputPinId), sizeof(int));
                                item.connections.push_back(conn);
                            }
                            
                            // Read thumbnail data
                            if (matFile.good()) {
                                matFile.read(reinterpret_cast<char*>(&item.thumbnailWidth), sizeof(uint32_t));
                                matFile.read(reinterpret_cast<char*>(&item.thumbnailHeight), sizeof(uint32_t));
                                size_t pixelDataSize;
                                matFile.read(reinterpret_cast<char*>(&pixelDataSize), sizeof(size_t));
                                
                                // Sanity check pixel data size (max ~16MB for 2048x2048 RGBA)
                                if (pixelDataSize > 0 && pixelDataSize < 20000000 && matFile.good()) {
                                    item.thumbnailPixels.resize(pixelDataSize);
                                    matFile.read(reinterpret_cast<char*>(item.thumbnailPixels.data()), pixelDataSize);
                                }
                                
                                // Read splitter ratio
                                if (matFile.good()) {
                                    matFile.read(reinterpret_cast<char*>(&item.splitterRatio), sizeof(float));
                                    // Sanity check splitter ratio
                                    if (item.splitterRatio < 0.1f || item.splitterRatio > 0.9f) {
                                        item.splitterRatio = 0.5f;
                                    }
                                }
                            }
                            
                            CrashHandler::logInfo("Loaded " + std::to_string(item.nodes.size()) + " nodes and " +
                                                  std::to_string(item.connections.size()) + " connections for material '" + nameWithoutExt + "'");
                            
                            CrashHandler::logInfo("Loaded material '" + nameWithoutExt + "' (ID: " + item.id + ") with color: " + 
                                std::to_string(item.materialColor.r) + ", " + 
                                std::to_string(item.materialColor.g) + ", " + 
                                std::to_string(item.materialColor.b));
                        } else {
                            CrashHandler::logWarning("Material file '" + nameWithoutExt + "' has invalid header");
                        }
                        matFile.close();
                    } else {
                        CrashHandler::logError("Failed to open material file: " + entry.path().string());
                    }
                    
                    m_contentItems.push_back(item);
                    
                    // Create thumbnail - use stored pixel data if available, otherwise generate
                    if (!m_contentItems.back().thumbnailPixels.empty() && 
                        m_contentItems.back().thumbnailWidth > 0 && 
                        m_contentItems.back().thumbnailHeight > 0) {
                        // Use stored thumbnail data from file
                        vkDeviceWaitIdle(m_vulkanContext->getDevice());
                        m_contentItems.back().thumbnailTexture = m_thumbnailGenerator->createThumbnailFromPixels(
                            m_contentItems.back().thumbnailPixels.data(),
                            m_contentItems.back().thumbnailWidth,
                            m_contentItems.back().thumbnailHeight);
                        CrashHandler::logInfo("Restored thumbnail from file for material: " + m_contentItems.back().name);
                    } else {
                        // Generate thumbnail from material color (legacy v4 or older)
                        generateMaterialThumbnail(m_contentItems.back(), m_contentItems.back().materialColor);
                    }
                    
                } else if (extension == ".fobj") {
                    // Object file - load object data
                    ContentItem item(nameWithoutExt, "Object", folderName, relativePathStr);
                    item.creationDate = creationDate;
                    
                    // Read object data from file (any version; see project/ObjectFile.h)
                    std::ifstream objFile(entry.path(), std::ios::binary);
                    if (objFile.is_open()) {
                        ObjectFile::Data data;
                        if (ObjectFile::read(objFile, data)) {
                            item.id = data.id;
                            item.meshReferenceGUID = data.meshGUID;
                            item.materialReferenceGUID = data.materialGUID;
                            item.scriptFile = data.scriptFile;
                            if (data.version == 1) {
                                item.meshReference = data.legacyMeshIndex;
                                item.materialReference = data.legacyMaterialIndex;
                                CrashHandler::logInfo("Legacy object needs GUID migration: " + nameWithoutExt);
                            } else {
                                CrashHandler::logInfo("Loaded object (v" + std::to_string(data.version) + "): " + nameWithoutExt +
                                                      " meshGUID=" + item.meshReferenceGUID +
                                                      (item.scriptFile.empty() ? "" : " script=" + item.scriptFile));
                            }
                        } else {
                            CrashHandler::logWarning("Object file '" + nameWithoutExt + "' could not be read");
                        }
                        objFile.close();
                    }
                    
                    m_contentItems.push_back(item);
                    
                    // Generate thumbnail - will be done after all items loaded so GUIDs can be resolved
                    
                } else if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || 
                           extension == ".PNG" || extension == ".JPG" || extension == ".JPEG" ||
                           extension == ".bmp" || extension == ".BMP" || extension == ".tga" || 
                           extension == ".TGA" || extension == ".hdr" || extension == ".HDR") {
                    // Texture file
                    ContentItem item(nameWithoutExt, "Texture", folderName, relativePathStr);
                    item.creationDate = creationDate;
                    
                    // Store full path for texture loading
                    std::filesystem::path fullPath = entry.path();
                    item.filepath = fullPath.string();
                    
                    m_contentItems.push_back(item);
                    
                    // Generate thumbnail from the actual texture
                    VkDescriptorSet thumbnail = generateTextureThumbnail(fullPath.string());
                    if (thumbnail != VK_NULL_HANDLE) {
                        m_contentItems.back().thumbnailTexture = thumbnail;
                    }
                    
                    CrashHandler::logInfo("Loaded texture: " + nameWithoutExt + " from " + fullPath.string());
                }
            }
        }
    }
    
    CrashHandler::logInfo("Asset scan complete. Found " + std::to_string(m_folders.size()) + " folders and " + std::to_string(m_contentItems.size()) + " items");
    
    // Generate thumbnails for objects (now that all meshes are loaded)
    for (auto& item : m_contentItems) {
        if (item.type == "Object") {
            Mesh* mesh = m_cube.get(); // Default
            
            // Look up mesh by GUID first
            if (!item.meshReferenceGUID.empty()) {
                for (const auto& meshItem : m_contentItems) {
                    if (meshItem.type == "Mesh" && meshItem.id == item.meshReferenceGUID) {
                        // Found the mesh - determine which built-in it is
                        if (meshItem.name == "Cube") mesh = m_cube.get();
                        else if (meshItem.name == "Sphere") mesh = m_sphere.get();
                        else if (meshItem.name == "Cylinder") mesh = m_cylinder.get();
                        // TODO: Support custom meshes
                        break;
                    }
                }
            } else if (item.meshReference >= 0 && item.meshReference < 3) {
                // Legacy index fallback
                switch (item.meshReference) {
                    case 0: mesh = m_cube.get(); break;
                    case 1: mesh = m_sphere.get(); break;
                    case 2: mesh = m_cylinder.get(); break;
                }
            }
            
            generateThumbnail(item, mesh);
        }
    }
    
    // Update tabs to point to correct content items after rescan (using asset ID)
    for (auto& tab : m_tabs) {
        if (tab.type == TabType::AssetEditor && !tab.assetId.empty()) {
            // Find the content item with matching ID
            int newIndex = -1;
            for (size_t i = 0; i < m_contentItems.size(); i++) {
                if (m_contentItems[i].id == tab.assetId) {
                    newIndex = static_cast<int>(i);
                    // Update tab name if item was renamed externally
                    if (tab.name != m_contentItems[i].name) {
                        CrashHandler::logInfo("Updating tab name from '" + tab.name + "' to '" + m_contentItems[i].name + "' after filesystem scan");
                        tab.name = m_contentItems[i].name;
                    }
                    break;
                }
            }
            
            // Update the content item index
            if (newIndex >= 0) {
                tab.contentItemIndex = newIndex;
            } else {
                // Asset was deleted or moved - keep old index but log warning
                CrashHandler::logWarning("Tab '" + tab.name + "' references missing asset ID: " + tab.assetId);
            }
        }
    }
    
    m_selectedContentIndex = -1; // Clear selection after scan
}

std::string Engine::generateGUID() {
    // Delegate to utility function
    return ForgeEngine::Utils::generateGUID();
}

void Engine::markProjectModified() {
    m_projectModified = true;
}

void Engine::renderProjectNameDialog() {
    ImGui::SetNextWindowSize(ImVec2(ForgeEngine::UI::scaled(400), ForgeEngine::UI::scaled(150)), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    
    if (ImGui::Begin("New Project", &m_showProjectNameDialog, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Enter project name:");
        ImGui::Spacing();
        
        ImGui::SetNextItemWidth(-1);
        bool enterPressed = ImGui::InputText("##projectname", m_projectNameBuffer, sizeof(m_projectNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("Create", ImVec2(ForgeEngine::UI::scaled(120), 0)) || enterPressed) {
            if (strlen(m_projectNameBuffer) > 0) {
                m_showProjectNameDialog = false;
                
                std::string projectName = std::string(m_projectNameBuffer);
                std::string selectedPath = ForgeEngine::Platform::showNewProjectFolderDialog(m_window->getNativeWindow(), projectName);
                
                if (!selectedPath.empty()) {
                    // Create full project path with .fproj file
                    std::string projectDir = selectedPath + "\\" + projectName;
                    std::string projectFilePath = projectDir + "\\" + projectName + ".fproj";
                    newProject();  // Initialize folders and default meshes
                    saveProject(projectFilePath);
                }
            }
        }
        
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(ForgeEngine::UI::scaled(120), 0))) {
            m_showProjectNameDialog = false;
        }
        
        ImGui::End();
    }
}

void Engine::renderPreferencesWindow() {
    if (!m_showPreferencesWindow) return;
    
    ImGui::SetNextWindowSize(ImVec2(ForgeEngine::UI::scaled(500), ForgeEngine::UI::scaled(400)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), 
                             ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    
    if (ImGui::Begin("Preferences", &m_showPreferencesWindow)) {
        ImGui::Text("UI Scaling");
        ImGui::Separator();
        ImGui::Spacing();

        const float displayScale = m_window->getContentScale();
        if (ImGui::Checkbox("Match display scale", &m_uiScaleAuto)) {
            // Switching to manual keeps the current look instead of jumping
            if (!m_uiScaleAuto) {
                m_uiScalePref = ForgeEngine::UI::getUIScale();
            }
            m_uiScaleDirty = true;
            savePreferences();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(display: %.0f%%)", displayScale * 100.0f);

        ImGui::Spacing();
        ImGui::Text("Scale");

        // Fixed width, label above: the slider must not resize as the UI rescales under the cursor
        // while dragging, or the mouse->value mapping drifts.
        ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.6f);
        float percent = (m_uiScaleAuto ? ForgeEngine::UI::getUIScale() : m_uiScalePref) * 100.0f;
        if (ImGui::SliderFloat("##UIScale", &percent, 50.0f, 300.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            m_uiScalePref = percent / 100.0f;
            m_uiScaleAuto = false;
            m_uiScaleDirty = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            savePreferences();  // Persist on release, not on every frame of the drag
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            m_uiScaleAuto = true;
            m_uiScaleDirty = true;
            savePreferences();
        }
        if (ImGui::IsItemHovered()) {
            ForgeEngine::UI::tooltip("Go back to matching the display scale");
        }

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("Rendering Settings");
        ImGui::Separator();
        ImGui::Spacing();
        
        // Exposure settings
        ImGui::Text("Exposure");
        ImGui::Spacing();
        
        if (ImGui::Checkbox("Auto Exposure", &m_autoExposure)) {
            CrashHandler::logInfo("Auto-exposure toggled: " + std::string(m_autoExposure ? "ON" : "OFF"));
            if (m_autoExposure) {
                m_targetExposure = calculateSceneLuminance();
                CrashHandler::logInfo("Calculated target exposure: " + std::to_string(m_targetExposure));
            }
        }
        
        ImGui::BeginDisabled(m_autoExposure);
        if (ImGui::SliderFloat("Manual Exposure", &m_exposure, 0.1f, 5.0f, "%.2f")) {
            if (!m_autoExposure) {
                m_targetExposure = m_exposure;
                CrashHandler::logInfo("Manual exposure set: " + std::to_string(m_exposure));
            }
        }
        ImGui::EndDisabled();
        
        if (m_autoExposure) {
            ImGui::Text("Current Exposure: %.2f", m_exposure);
        }
        
        ImGui::Spacing();
        if (ImGui::SliderFloat("Adaptation Speed", &m_exposureAdaptSpeed, 0.1f, 10.0f, "%.2f")) {
            CrashHandler::logInfo("Adaptation speed changed: " + std::to_string(m_exposureAdaptSpeed));
        }
        ImGui::SameLine();
        if (ImGui::Button("?")) {
            ForgeEngine::UI::tooltip("How quickly exposure adapts to scene changes");
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Selection Outline settings
        ImGui::Text("Selection Outline");
        ImGui::Spacing();
        
        // Color picker for outline
        float outlineColorArr[3] = { m_outlineColor.r, m_outlineColor.g, m_outlineColor.b };
        if (ImGui::ColorEdit3("Outline Color", outlineColorArr, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            m_outlineColor = glm::vec3(outlineColorArr[0], outlineColorArr[1], outlineColorArr[2]);
        }
        ImGui::SameLine();
        ImGui::Text("Outline Color");
        
        ImGui::Spacing();
        
        if (ImGui::SliderInt("Outline Thickness", &m_outlineThickness, 1, 15, "%d px")) {
            // Thickness updated
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Additional settings can go here
        ImGui::Text("Other Settings");
        ImGui::Spacing();
        ImGui::TextDisabled("(Coming soon...)");
    }
    ImGui::End();
}

float Engine::calculateSceneLuminance() {
    // Calculate average luminance based on object colors in scene
    // This is a simplified approximation
    if (m_sceneObjects.empty()) {
        return 1.0f;
    }
    
    float totalLuminance = 0.0f;
    int visibleCount = 0;
    
    for (const auto& obj : m_sceneObjects) {
        if (obj.visible) {
            // Calculate relative luminance using standard coefficients
            float luminance = 0.2126f * obj.color.r + 0.7152f * obj.color.g + 0.0722f * obj.color.b;
            totalLuminance += luminance;
            visibleCount++;
        }
    }
    
    if (visibleCount == 0) {
        return 1.0f;
    }
    
    float avgLuminance = totalLuminance / visibleCount;
    
    // Convert to exposure value (inverse of luminance with some scaling)
    // Brighter scenes need lower exposure, darker scenes need higher exposure
    float targetExposure = 0.5f / (avgLuminance + 0.1f);
    
    // Clamp to reasonable range
    return glm::clamp(targetExposure, 0.1f, 5.0f);
}

void Engine::renderStartupDialog() {
    // Make the dialog fill the window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    if (ImGui::Begin("Startup", nullptr, flags)) {
        // Animate logo gradient
        static SVGRenderer svgRenderer;
        static bool logoLoaded = false;
        
        if (!logoLoaded) {
            logoLoaded = svgRenderer.loadFromFile("resources/icons/felogo.svg");
        }
        
        if (logoLoaded) {
            // Update animation time (5 seconds per full cycle)
            // TODO: Animate logo with Vulkan texture rendering
        }
        
        // Center the content
        ImVec2 windowSize = ImGui::GetWindowSize();
        float dialogWidth = ForgeEngine::UI::scaled(450.0f);
        float dialogHeight = ForgeEngine::UI::scaled(400.0f);
        
        ImGui::SetCursorPos(ImVec2((windowSize.x - dialogWidth) * 0.5f, (windowSize.y - dialogHeight) * 0.5f));
        
        ImGui::BeginChild("StartupContent", ImVec2(dialogWidth, dialogHeight), true);
        
        // TODO: Display logo when Vulkan texture loading is implemented
        
        ImGui::Spacing();
        ImGui::Spacing();
        
        // Title
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Use default font
        float titleWidth = ImGui::CalcTextSize("Forge Engine").x;
        ImGui::SetCursorPosX((dialogWidth - titleWidth) * 0.5f);
        ImGui::Text("Forge Engine");
        ImGui::PopFont();
        
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Spacing();
        
        // Center buttons
        float buttonWidth = ForgeEngine::UI::scaled(200.0f);
        float buttonHeight = ForgeEngine::UI::scaled(40.0f);
        ImGui::SetCursorPosX((dialogWidth - buttonWidth) * 0.5f);
        
        if (ImGui::Button("Create New Project", ImVec2(buttonWidth, buttonHeight))) {
            m_showStartupDialog = false;
            m_showProjectNameDialog = true;
            m_isNewProject = true;
            strcpy_s(m_projectNameBuffer, "NewProject");
            m_engineInitialized = true;
        }
        
        ImGui::Spacing();
        ImGui::SetCursorPosX((dialogWidth - buttonWidth) * 0.5f);
        
        if (ImGui::Button("Open Existing Project", ImVec2(buttonWidth, buttonHeight))) {
            m_showStartupDialog = false;
            m_engineInitialized = true;
            
#ifdef _WIN32
            std::string filepath = ForgeEngine::Platform::showOpenFileDialog(m_window->getNativeWindow());
            if (!filepath.empty()) {
                loadProject(filepath);
            } else {
                // User cancelled - show dialog again
                m_showStartupDialog = true;
                m_engineInitialized = false;
            }
#endif
        }
        
        ImGui::EndChild();
        ImGui::End();
    }
}

void Engine::onWindowClose() {
    // User clicked the close button - show confirmation dialog
    m_showExitConfirmation = true;
}

void Engine::renderExitConfirmationDialog() {
    if (!m_showExitConfirmation) return;
    
    // Center the popup
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    // Determine dialog content based on unsaved changes
    const char* popupName = m_projectModified ? "Unsaved Changes###ExitConfirm" : "Confirm Exit###ExitConfirm";
    
    ImGui::OpenPopup(popupName);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;
    
    if (ImGui::BeginPopupModal(popupName, nullptr, flags)) {
        if (m_projectModified) {
            // Dialog for unsaved changes
            ImGui::Text("You have unsaved changes.");
            ImGui::Text("Are you sure you want to quit?");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            float buttonWidth = ForgeEngine::UI::scaled(110.0f);
            float totalWidth = buttonWidth * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
            float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);
            
            if (ImGui::Button("Save All", ImVec2(buttonWidth, 0))) {
                // Save all unsaved assets first
                saveAllUnsavedAssets();
                // Then save the project file
                if (!m_currentProjectPath.empty()) {
                    saveProject(m_currentProjectPath);
                    m_exitConfirmed = true;
                    m_showExitConfirmation = false;
                    m_window->setShouldClose(true);
                } else {
                    // Need to show save dialog first
                    m_showProjectNameDialog = true;
                    m_isNewProject = true;
                    strcpy_s(m_projectNameBuffer, "NewProject");
                    // After saving, the exit will be handled
                    m_exitConfirmed = true;
                    m_showExitConfirmation = false;
                }
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Discard Changes", ImVec2(buttonWidth, 0))) {
                // Discard changes and exit
                m_exitConfirmed = true;
                m_showExitConfirmation = false;
                m_window->setShouldClose(true);
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                // Cancel exit - stay in application
                m_showExitConfirmation = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            // Simple confirmation dialog (no unsaved changes)
            ImGui::Text("Are you sure you want to quit?");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            float buttonWidth = ForgeEngine::UI::scaled(100.0f);
            float totalWidth = buttonWidth * 2 + ImGui::GetStyle().ItemSpacing.x;
            float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);
            
            if (ImGui::Button("Confirm", ImVec2(buttonWidth, 0))) {
                // Confirm exit
                m_exitConfirmed = true;
                m_showExitConfirmation = false;
                m_window->setShouldClose(true);
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
                // Cancel exit - stay in application
                m_showExitConfirmation = false;
                ImGui::CloseCurrentPopup();
            }
        }
        
        ImGui::EndPopup();
    }
}

void Engine::renderTabCloseConfirmationDialog() {
    if (!m_showTabCloseConfirmation) return;
    
    // Validate tab index
    if (m_tabToClose < 0 || m_tabToClose >= static_cast<int>(m_tabs.size())) {
        m_showTabCloseConfirmation = false;
        m_tabToClose = -1;
        return;
    }
    
    Tab& tab = m_tabs[m_tabToClose];
    
    // Center the popup
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    ImGui::OpenPopup("Unsaved Changes###TabCloseConfirm");
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;
    
    if (ImGui::BeginPopupModal("Unsaved Changes###TabCloseConfirm", nullptr, flags)) {
        ImGui::Text("\"%s\" has unsaved changes.", tab.name.c_str());
        ImGui::Text("Would you like to save them?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        float buttonWidth = ForgeEngine::UI::scaled(110.0f);
        float totalWidth = buttonWidth * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
        float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);
        
        if (ImGui::Button("Save Changes", ImVec2(buttonWidth, 0))) {
            // Save the asset and close the tab
            if (tab.contentItemIndex >= 0 && tab.contentItemIndex < static_cast<int>(m_contentItems.size())) {
                ContentItem& item = m_contentItems[tab.contentItemIndex];
                
                if (item.type == "Material" && !m_currentProjectPath.empty() && !item.filepath.empty()) {
                    try {
                        std::filesystem::path projectFile(m_currentProjectPath);
                        std::filesystem::path projectDir = projectFile.parent_path();
                        std::filesystem::path materialFile = projectDir / item.filepath;
                        std::filesystem::create_directories(materialFile.parent_path());
                        
                        // Update full node data from the tab's node graph
                        if (tab.nodeGraph) {
                            item.nodes.clear();
                            item.connections.clear();
                            item.nodePositions.clear();
                            
                            for (const auto& node : tab.nodeGraph->getNodes()) {
                                // Save to nodes (new format)
                                MaterialNodeData nodeData;
                                nodeData.nodeType = node->type;
                                nodeData.nodeId = node->id;
                                nodeData.posX = node->position.x;
                                nodeData.posY = node->position.y;
                                
                                if (node->type == "ColorWheel") {
                                    ColorWheelNode* colorNode = dynamic_cast<ColorWheelNode*>(node.get());
                                    if (colorNode) {
                                        nodeData.color = colorNode->color;
                                        nodeData.hue = colorNode->hue;
                                        nodeData.saturation = colorNode->saturation;
                                        nodeData.value = colorNode->value;
                                    }
                                }
                                else if (node->type == "Image") {
                                    ImageNode* imageNode = dynamic_cast<ImageNode*>(node.get());
                                    if (imageNode) {
                                        nodeData.texturePath = imageNode->selectedTexturePath;
                                        nodeData.textureType = static_cast<int>(imageNode->textureType);
                                        nodeData.strength = imageNode->strength;
                                        nodeData.invertTexture = imageNode->invertTexture;
                                    }
                                }
                                item.nodes.push_back(nodeData);
                                
                                // Also save to nodePositions (legacy format)
                                item.nodePositions.emplace_back(
                                    node->type, 
                                    node->id, 
                                    node->position.x, 
                                    node->position.y
                                );
                            }
                            
                            // Save connections
                            for (const auto& conn : tab.nodeGraph->getConnections()) {
                                item.connections.emplace_back(
                                    conn.outputNodeId,
                                    conn.outputPinId,
                                    conn.inputNodeId,
                                    conn.inputPinId
                                );
                            }
                        }
                        
                        // Evaluate material color from saved nodes
                        glm::vec3 evaluatedColor = item.materialColor;
                        bool useAlbedoTexture = false;
                        std::string albedoTexturePath;
                        
                        int outputNodeId = -1;
                        for (const auto& nodeData : item.nodes) {
                            if (nodeData.nodeType == "MaterialOutput") {
                                outputNodeId = nodeData.nodeId;
                                break;
                            }
                        }
                        if (outputNodeId >= 0) {
                            int sourceNodeId = -1;
                            for (const auto& conn : item.connections) {
                                if (conn.inputNodeId == outputNodeId && conn.inputPinId == 1) {
                                    sourceNodeId = conn.outputNodeId;
                                    break;
                                }
                            }
                            if (sourceNodeId >= 0) {
                                for (const auto& nodeData : item.nodes) {
                                    if (nodeData.nodeId == sourceNodeId) {
                                        if (nodeData.nodeType == "ColorWheel") {
                                            evaluatedColor = nodeData.color;
                                        } else if (nodeData.nodeType == "Image" && !nodeData.texturePath.empty()) {
                                            useAlbedoTexture = true;
                                            albedoTexturePath = nodeData.texturePath;
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                        item.materialColor = evaluatedColor;
                        
                        // Generate thumbnail FIRST (populates item.thumbnailPixels for file)
                        generateMaterialThumbnailWithTexture(item, evaluatedColor, useAlbedoTexture, albedoTexturePath);
                        
                        std::ofstream matFile(materialFile, std::ios::binary);
                        if (matFile.is_open()) {
                            // Write header
                            matFile.write("MAT", 3);
                            int formatMarker = 1;
                            matFile.write(reinterpret_cast<const char*>(&formatMarker), sizeof(int));
                            
                            // Write GUID
                            size_t guidLength = item.id.length();
                            matFile.write(reinterpret_cast<const char*>(&guidLength), sizeof(size_t));
                            matFile.write(item.id.c_str(), guidLength);
                            
                            // Write color
                            matFile.write(reinterpret_cast<const char*>(&item.materialColor.r), sizeof(float));
                            matFile.write(reinterpret_cast<const char*>(&item.materialColor.g), sizeof(float));
                            matFile.write(reinterpret_cast<const char*>(&item.materialColor.b), sizeof(float));
                            
                            // Write full node data
                            size_t nodeCount = item.nodes.size();
                            matFile.write(reinterpret_cast<const char*>(&nodeCount), sizeof(size_t));
                            for (const auto& nodeData : item.nodes) {
                                size_t typeLength = nodeData.nodeType.length();
                                matFile.write(reinterpret_cast<const char*>(&typeLength), sizeof(size_t));
                                matFile.write(nodeData.nodeType.c_str(), typeLength);
                                matFile.write(reinterpret_cast<const char*>(&nodeData.nodeId), sizeof(int));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.posX), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.posY), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.color.r), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.color.g), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.color.b), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.hue), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.saturation), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.value), sizeof(float));
                                // Image node data (v4)
                                size_t pathLength = nodeData.texturePath.length();
                                matFile.write(reinterpret_cast<const char*>(&pathLength), sizeof(size_t));
                                if (pathLength > 0) {
                                    matFile.write(nodeData.texturePath.c_str(), pathLength);
                                }
                                matFile.write(reinterpret_cast<const char*>(&nodeData.textureType), sizeof(int));
                                // Texture strength (v6)
                                matFile.write(reinterpret_cast<const char*>(&nodeData.strength), sizeof(float));
                                // Invert texture flag (v7)
                                uint8_t invertFlag = nodeData.invertTexture ? 1 : 0;
                                matFile.write(reinterpret_cast<const char*>(&invertFlag), sizeof(uint8_t));
                            }
                            
                            // Write connections
                            size_t connCount = item.connections.size();
                            matFile.write(reinterpret_cast<const char*>(&connCount), sizeof(size_t));
                            for (const auto& conn : item.connections) {
                                matFile.write(reinterpret_cast<const char*>(&conn.outputNodeId), sizeof(int));
                                matFile.write(reinterpret_cast<const char*>(&conn.outputPinId), sizeof(int));
                                matFile.write(reinterpret_cast<const char*>(&conn.inputNodeId), sizeof(int));
                                matFile.write(reinterpret_cast<const char*>(&conn.inputPinId), sizeof(int));
                            }
                            
                            // Write thumbnail data (v5)
                            matFile.write(reinterpret_cast<const char*>(&item.thumbnailWidth), sizeof(uint32_t));
                            matFile.write(reinterpret_cast<const char*>(&item.thumbnailHeight), sizeof(uint32_t));
                            size_t pixelDataSize = item.thumbnailPixels.size();
                            matFile.write(reinterpret_cast<const char*>(&pixelDataSize), sizeof(size_t));
                            if (pixelDataSize > 0) {
                                matFile.write(reinterpret_cast<const char*>(item.thumbnailPixels.data()), pixelDataSize);
                            }
                            
                            // Write splitter ratio
                            matFile.write(reinterpret_cast<const char*>(&item.splitterRatio), sizeof(float));
                            
                            matFile.close();
                            CrashHandler::logInfo("Saved material (v5) before closing tab: " + item.name);
                            
                            // Update scene objects using this material
                            for (auto& obj : m_sceneObjects) {
                                if (objectUsesMaterial(obj, tab.contentItemIndex)) {
                                    obj.color = evaluatedColor;
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        CrashHandler::logError("Exception saving material: " + std::string(e.what()));
                    }
                }
            }
            
            // Remove from unsaved tracking
            m_unsavedContentItems.erase(tab.contentItemIndex);
            
            // Close the tab
            m_tabs.erase(m_tabs.begin() + m_tabToClose);
            if (m_activeTabIndex >= static_cast<int>(m_tabs.size())) {
                m_activeTabIndex = static_cast<int>(m_tabs.size()) - 1;
            }
            
            // Update project modified status
            m_projectModified = !m_unsavedContentItems.empty();
            
            m_showTabCloseConfirmation = false;
            m_tabToClose = -1;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Discard Changes", ImVec2(buttonWidth, 0))) {
            // Remove from unsaved tracking without saving
            m_unsavedContentItems.erase(tab.contentItemIndex);
            
            // Close the tab without saving
            m_tabs.erase(m_tabs.begin() + m_tabToClose);
            if (m_activeTabIndex >= static_cast<int>(m_tabs.size())) {
                m_activeTabIndex = static_cast<int>(m_tabs.size()) - 1;
            }
            
            // Update project modified status
            m_projectModified = !m_unsavedContentItems.empty();
            
            m_showTabCloseConfirmation = false;
            m_tabToClose = -1;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            // Cancel - don't close the tab
            m_showTabCloseConfirmation = false;
            m_tabToClose = -1;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

void Engine::renderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project", "Ctrl+N")) {
                // TODO: Add unsaved changes warning
                newProject();
            }
            
            if (ImGui::MenuItem("Open Project...")) {
#ifdef _WIN32
                std::string filepath = ForgeEngine::Platform::showOpenFileDialog(m_window->getNativeWindow());
                if (!filepath.empty()) {
                    loadProject(filepath);
                }
#else
                // For non-Windows, use test path
                loadProject("test_project.fproj");
#endif
            }
            
            ImGui::Separator();
            
            if (ImGui::MenuItem("Save All", "Ctrl+Shift+S")) {
                // Save all unsaved assets first
                saveAllUnsavedAssets();
                // Then save the project file
                if (!m_currentProjectPath.empty()) {
                    saveProject(m_currentProjectPath);
                } else {
                    // Show project name dialog
                    m_showProjectNameDialog = true;
                    m_isNewProject = true;
                    strcpy_s(m_projectNameBuffer, "NewProject");
                }
            }
            
            if (ImGui::MenuItem("Save Project As...")) {
                std::string currentName = "NewProject";
                if (!m_currentProjectPath.empty()) {
                    std::filesystem::path p(m_currentProjectPath);
                    currentName = p.filename().string();
                }
                m_showProjectNameDialog = true;
                m_isNewProject = false;
                strcpy_s(m_projectNameBuffer, currentName.c_str());
            }
            
            ImGui::Separator();
            
            if (ImGui::MenuItem("Preferences...", "Ctrl+,")) {
                m_showPreferencesWindow = true;
            }
            
            ImGui::Separator();
            
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                onWindowClose();  // Use same confirmation dialog as close button
            }
            
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undoStack.empty())) {
                undo();
            }
            
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redoStack.empty())) {
                redo();
            }
            
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Objects Panel", nullptr, &m_showObjectsPanel);
            ImGui::MenuItem("Content Browser", nullptr, &m_showContentBrowser);
            ImGui::MenuItem("World Grid", nullptr, &m_showWorldGrid);
            ImGui::Separator();
            ImGui::MenuItem("FPS Counter", "F3", &m_showFPS);
            ImGui::MenuItem("Statistics", nullptr, &m_showStatistics);
            
            // Debug Output opens as a tab
            if (ImGui::MenuItem("Debug Output")) {
                openDebugOutputTab();
            }
            
            ImGui::EndMenu();
        }

        // Play / Stop: runs ForgeScript scripts on a copy of the scene
        if (ImGui::MenuItem(m_isPlaying ? "Stop" : "Play")) {
            if (m_isPlaying) stopPlay();
            else startPlay();
        }

        // Show project status in menu bar
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 300);
        std::string status;
        if (!m_currentProjectPath.empty()) {
            size_t lastSlash = m_currentProjectPath.find_last_of("/\\");
            std::string filename = (lastSlash != std::string::npos) 
                ? m_currentProjectPath.substr(lastSlash + 1) 
                : m_currentProjectPath;
            status = filename + (m_projectModified ? " *" : "");
        } else {
            status = "Untitled" + std::string(m_projectModified ? " *" : "");
        }
        ImGui::Text("%s", status.c_str());
        
        ImGui::EndMainMenuBar();
    }
}

} // namespace Forge



namespace Forge {

void Engine::openProject(const std::string& filepath) {
    m_showStartupDialog = false;
    m_engineInitialized = true;
    loadProject(filepath);
}

} // namespace Forge

namespace Forge {

int Engine::materialIndexOf(const SceneObject& obj) const {
    if (!obj.materialGUID.empty()) {
        for (size_t i = 0; i < m_contentItems.size(); i++) {
            if (m_contentItems[i].type == "Material" && m_contentItems[i].id == obj.materialGUID) {
                return static_cast<int>(i);
            }
        }
        return -1;  // it names a material that isn't in the project (any more)
    }
    if (obj.materialIndex >= 0 && obj.materialIndex < static_cast<int>(m_contentItems.size()) &&
        m_contentItems[obj.materialIndex].type == "Material") {
        return obj.materialIndex;
    }
    return -1;
}

bool Engine::objectUsesMaterial(const SceneObject& obj, int itemIndex) const {
    return itemIndex >= 0 && materialIndexOf(obj) == itemIndex;
}

const ContentItem* Engine::sourceAssetOf(const SceneObject& obj) const {
    if (obj.sourceObjectGUID.empty()) return nullptr;
    for (const ContentItem& item : m_contentItems) {
        if (item.type == "Object" && item.id == obj.sourceObjectGUID) return &item;
    }
    return nullptr;  // placed from an asset that isn't in the project any more: it keeps what it has
}

void Engine::applyAssetDefaults(SceneObject& obj) const {
    const ContentItem* asset = sourceAssetOf(obj);
    if (!asset) return;

    if (!obj.overridesMesh) {
        obj.meshGUID = asset->meshReferenceGUID;
        obj.meshIndex = asset->meshReferenceGUID.empty() ? asset->meshReference : -1;
    }
    if (!obj.overridesMaterial) {
        obj.materialGUID = asset->materialReferenceGUID;
        obj.materialIndex = asset->materialReferenceGUID.empty() ? asset->materialReference : -1;
        const int material = materialIndexOf(obj);
        obj.color = material >= 0 ? m_contentItems[material].materialColor : glm::vec3(0.7f);
    }
}

void Engine::syncInstancesOf(const std::string& assetGUID) {
    for (SceneObject& obj : m_sceneObjects) {
        if (obj.sourceObjectGUID == assetGUID) applyAssetDefaults(obj);
    }
}

void Engine::inferOverrides(SceneObject& obj) const {
    const ContentItem* asset = sourceAssetOf(obj);
    if (!asset) return;
    obj.overridesMesh = !obj.meshGUID.empty() && obj.meshGUID != asset->meshReferenceGUID;
    obj.overridesMaterial = !obj.materialGUID.empty() && obj.materialGUID != asset->materialReferenceGUID;
}

// Setting a property to the asset's own default isn't an override: the instance goes on following it.
void Engine::setInstanceMesh(SceneObject& obj, int itemIndex) {
    obj.meshGUID = m_contentItems[itemIndex].id;
    obj.meshIndex = itemIndex;
    const ContentItem* asset = sourceAssetOf(obj);
    obj.overridesMesh = asset && obj.meshGUID != asset->meshReferenceGUID;
    if (asset && !obj.overridesMesh) applyAssetDefaults(obj);
}

void Engine::setInstanceMaterial(SceneObject& obj, int itemIndex) {
    obj.materialGUID = m_contentItems[itemIndex].id;  // the GUID is what survives a save and reload
    obj.materialIndex = itemIndex;
    obj.color = m_contentItems[itemIndex].materialColor;
    const ContentItem* asset = sourceAssetOf(obj);
    obj.overridesMaterial = asset && obj.materialGUID != asset->materialReferenceGUID;
    if (asset && !obj.overridesMaterial) applyAssetDefaults(obj);
}

void Engine::deleteObject(int index) {
    if (m_isPlaying) {
        m_consoleBuffer.push_back("Stop Play before deleting objects.");
        return;
    }
    if (index < 0 || index >= static_cast<int>(m_sceneObjects.size())) return;

    std::string name = m_sceneObjects[index].name;
    m_sceneObjects.erase(m_sceneObjects.begin() + index);

    // Selection, the gizmo, and undo all refer to objects by position, which just changed.
    m_selectedObjectIndex = -1;
    m_gizmoActive = false;
    m_undoStack.clear();
    m_redoStack.clear();

    markProjectModified();
    m_consoleBuffer.push_back("Deleted object: " + name);
}

} // namespace Forge
