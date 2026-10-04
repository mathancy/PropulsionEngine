#pragma once

#include "core/Window.h"
#include "core/Camera.h"
#include "rendering/Shader.h"
#include "rendering/Mesh.h"
#include "rendering/VulkanContext.h"
#include "ui/GizmoTypes.h"
#include "ui/TabSystem.h"
#include "ui/ContentItem.h"
#include "rendering/WorldGrid.h"
#include "core/CrashHandler.h"
#include "rendering/ThumbnailGenerator.h"

// New Object/Component system (Phase 1 implementation)
#include "core/Object.h"
#include "core/Component.h"
#include "core/TransformComponent.h"
#include "core/MeshRendererComponent.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <memory>
#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <ctime>

namespace Forge {

/**
 * SceneObject - Legacy struct for scene entities.
 * 
 * MIGRATION NOTE: This struct is being replaced by the Object/Component system.
 * New code should use Object class with TransformComponent and MeshRendererComponent.
 * This struct is kept for backward compatibility during migration.
 * 
 * To migrate:
 *   SceneObject obj("Name")  -->  Object obj("Name")
 *   obj.position             -->  obj.getTransform()->position
 *   obj.meshIndex            -->  obj.getComponent<MeshRendererComponent>()->meshIndex
 */
struct SceneObject {
    std::string name;
    glm::vec3 position;
    glm::vec3 rotation;  // Euler angles in degrees (for UI display)
    glm::quat quaternion; // Actual rotation storage
    glm::vec3 scale;
    glm::vec3 color;     // Object color
    bool visible;
    int sourceObjectIndex;  // Index of the ContentItem Object this was created from (-1 if none)
    int meshIndex;          // Index to mesh ContentItem (-1 for none) - DEPRECATED
    int materialIndex;      // Index to material ContentItem (-1 for none) - DEPRECATED
    std::string meshGUID;       // GUID of the mesh ContentItem
    std::string materialGUID;   // GUID of the material ContentItem
    std::string sourceObjectGUID;  // GUID of the ContentItem Object this was created from
    // Blueprint-style defaults: the source Object asset holds the defaults, and an instance follows them
    // until the property is set to something else on the instance. Changing the asset's default then
    // updates every instance that isn't overriding that property.
    bool overridesMesh = false;
    bool overridesMaterial = false;
    std::string scriptFile;    // Runtime only, never saved: filled in when Play starts from the source Object asset's script (empty = nothing runs)

    SceneObject(const std::string& n) 
        : name(n), position(0.0f), rotation(0.0f), quaternion(1.0f, 0.0f, 0.0f, 0.0f), scale(1.0f), 
          color(0.7f, 0.7f, 0.7f), visible(true), sourceObjectIndex(-1), meshIndex(-1), materialIndex(-1) {}
};

struct UndoState {
    int objectIndex;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::quat quaternion;
    glm::vec3 scale;
    
    UndoState(int idx, const glm::vec3& pos, const glm::vec3& rot, const glm::quat& quat, const glm::vec3& scl)
        : objectIndex(idx), position(pos), rotation(rot), quaternion(quat), scale(scl) {}
};

class PlayMode;

class Engine {
public:
    Engine(int width = 1280, int height = 720);
    ~Engine();

    void run();
    
    void saveProject(const std::string& filepath);
    void loadProject(const std::string& filepath);
    void newProject();
    void openProject(const std::string& filepath);  // skip the startup dialog and open this .fproj (also: ForgeEngine.exe <project.fproj>)
    void openScript(const std::string& scriptPath) { openScriptGraphTab(scriptPath); }  // after openProject; ForgeEngine.exe <project.fproj> <Scripts/x.forge>: open its graph
    void saveAllUnsavedAssets();  // Save all assets in m_unsavedContentItems
    void scanAssetsDirectory(bool scanAllFolders = false); // Scan filesystem and rebuild content items
    std::string generateGUID(); // Generate a unique identifier for assets
    
private:
    void init();
    void initImGui();
    void applyUIScale();
    void loadToolIcons();
    void processInput(float deltaTime);
    void update(float deltaTime);
    void render();
    void renderUI();
    void renderTabs();
    void renderSceneTab();
    void renderAssetEditorTab(Tab& tab);
    void renderAssetPreview(Tab& tab, VkCommandBuffer commandBuffer);
    void renderObjectsPanel();
    void renderScriptGraphTab(Tab& tab);
    void openScriptGraphTab(const std::string& scriptPath);  // path relative to the project folder
    void renderContentBrowser();
    void renderGizmo(VkCommandBuffer commandBuffer, const glm::mat4& view, const glm::mat4& projection);
    void renderToolButtons();
    void shutdown();
    
    // Play mode: runs ForgeScript scripts on a copy of the scene, then restores the scene on stop.
    void startPlay();
    void stopPlay();

    void checkViewportClick();
    bool rayCastObject(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const SceneObject& obj, float& distance);
    bool rayCastGizmo(const glm::vec3& rayOrigin, const glm::vec3& rayDir, GizmoAxis& hitAxis);
    void handleGizmoInteraction();
    Mesh* getMeshForObject(const SceneObject& obj) const;
    glm::vec3 getMaterialColorForObject(const SceneObject& obj) const;  // Get evaluated material color
    
    // Materials are found by GUID first (what Object assets and saved projects store), then by the
    // legacy index. -1 if the object has no material.
    int materialIndexOf(const SceneObject& obj) const;
    bool objectUsesMaterial(const SceneObject& obj, int itemIndex) const;
    void deleteObject(int index);

    // Object assets are the defaults for the instances placed from them.
    const ContentItem* sourceAssetOf(const SceneObject& obj) const;
    void applyAssetDefaults(SceneObject& obj) const;   // copies the asset's mesh/material into every property not overridden
    void syncInstancesOf(const std::string& assetGUID); // after an asset's defaults change
    void inferOverrides(SceneObject& obj) const;        // for a project saved before overrides were recorded
    void setInstanceMesh(SceneObject& obj, int itemIndex);
    void setInstanceMaterial(SceneObject& obj, int itemIndex);

    void pushUndoState();
    void undo();
    void redo();
    bool graphTabUndo(bool redo);  // undo/redo in the active node graph tab; false if the active tab isn't one
    
    void syncEulerFromQuaternion(SceneObject& obj);
    void syncQuaternionFromEuler(SceneObject& obj);
    
    void startRotationLog();
    void logRotationFrame();
    void endRotationLog();
    
    void onResize(int width, int height);
    void onMouseButton(int button, int action, int mods);
    void onMouseMove(double xpos, double ypos);

    std::unique_ptr<Window> m_window;
    std::unique_ptr<VulkanContext> m_vulkanContext;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<Shader> m_shader;              // Main shader for swap chain rendering
    std::unique_ptr<Shader> m_offscreenShader;     // Shader for offscreen rendering
    std::unique_ptr<Shader> m_maskShader;          // Shader for selection mask rendering
    std::unique_ptr<Shader> m_gizmoShader;
    std::unique_ptr<Mesh> m_cube;
    std::unique_ptr<Mesh> m_sphere;
    std::unique_ptr<Mesh> m_cylinder;
    
    // Gizmo-specific meshes (procedurally generated for consistent unit size)
    std::unique_ptr<Mesh> m_gizmoCube;
    std::unique_ptr<Mesh> m_gizmoCone;
    std::unique_ptr<Mesh> m_gizmoTorus;
    std::unique_ptr<Mesh> m_gizmoDisc;
    std::unique_ptr<WorldGrid> m_worldGrid;
    std::unique_ptr<ThumbnailGenerator> m_thumbnailGenerator;
    
    // Vulkan textures (replacing OpenGL GLuint)
    VkImage m_logoImage;
    VkDeviceMemory m_logoImageMemory;
    VkImageView m_logoImageView;
    VkSampler m_logoSampler;
    VkDescriptorSet m_logoDescriptorSet;
    float m_logoAnimationTime;
    ImFont* m_fontRegular;
    ImFont* m_fontBold;
    ImFont* m_fontItalic;
    ImFont* m_fontBoldItalic;
    
    // Viewport (Vulkan swap chain replaces OpenGL framebuffer)
    int m_viewportWidth;
    int m_viewportHeight;
    ImVec2 m_viewportPos;
    ImVec2 m_viewportSize;
    bool m_viewportHovered;
    bool m_viewportFocused;
    
    // Viewport view mode (always Composite for final output)
    enum class ViewMode { Composite };
    ViewMode m_viewMode = ViewMode::Composite;
    
    // Camera control
    bool m_firstMouse;
    float m_lastX;
    float m_lastY;
    bool m_rightMousePressed;
    
    // Timing
    float m_deltaTime;
    float m_lastFrame;
    float m_targetFrameTime; // For FPS cap (1/240 = ~0.00417)
    
    // UI state
    bool m_showViewport;
    bool m_showFPS;
    bool m_showStatistics = false;
    bool m_showObjectsPanel;
    bool m_showContentBrowser;
    bool m_showWorldGrid;
    bool m_firstFrame;
    bool m_dockingLayoutInitialized;
    ImVec2 m_lastDockSpaceSize;
    
    // Debug console
    std::vector<std::string> m_consoleBuffer;
    void renderDebugConsole();
    void renderDebugOutputTab();
    void openDebugOutputTab();
    void generateThumbnail(ContentItem& item, Mesh* mesh);
    void generateMaterialThumbnail(ContentItem& item, const glm::vec3& color);
    void generateMaterialThumbnailWithTexture(ContentItem& item, const glm::vec3& color, bool useTexture, const std::string& texturePath);
    void createMaterial(const std::string& name);
    void createObject(const std::string& name);
    void importFile(const std::string& filepath);
    std::string getAssetTypeFromExtension(const std::string& extension);
    VkDescriptorSet generateTextureThumbnail(const std::string& filepath);
    
    // Import file state
    bool m_showImportDialog;
    
    // Tab system
    std::vector<Tab> m_tabs;
    int m_activeTabIndex;
    float m_tabBarHeight;
    
    // Scene data
    std::vector<SceneObject> m_sceneObjects;
    int m_selectedObjectIndex;

    // Play mode state
    bool m_isPlaying = false;
    std::unique_ptr<PlayMode> m_playMode;
    std::vector<SceneObject> m_playSnapshot;  // the scene as it was when Play was pressed
    
    // Gizmo system
    GizmoMode m_gizmoMode;
    GizmoAxis m_hoveredGizmoAxis;
    GizmoAxis m_activeGizmoAxis;
    bool m_gizmoActive;
    glm::vec3 m_gizmoStartPos;
    glm::vec3 m_gizmoStartScale;
    glm::quat m_gizmoStartQuaternion;
    glm::vec3 m_gizmoInteractionStart;
    glm::vec3 m_dragStartMouseWorld;
    float m_gizmoStartAxisT;  // Initial t-parameter along axis when drag started
    float m_rotationAccumulatedAngle; // Accumulated rotation angle in radians
    float m_rotationStartAngle; // Angle on rotation plane where drag started (radians)
    float m_rotationCurrentAngle; // Current angle on rotation plane (radians)
    bool m_rotationIsPositive; // Direction of rotation (true = positive/CCW)
    void renderWorldAxes();
    void renderRotationFeedbackDisc();
    
    // Tool icons (Vulkan textures)
    VkImage m_moveIconImage;
    VkDeviceMemory m_moveIconMemory;
    VkImageView m_moveIconView;
    VkDescriptorSet m_moveIconDescriptor;
    
    VkImage m_rotateIconImage;
    VkDeviceMemory m_rotateIconMemory;
    VkImageView m_rotateIconView;
    VkDescriptorSet m_rotateIconDescriptor;
    
    VkImage m_scaleIconImage;
    VkDeviceMemory m_scaleIconMemory;
    VkImageView m_scaleIconView;
    VkDescriptorSet m_scaleIconDescriptor;
    
    // Logo texture (TODO: implement Vulkan texture loading)
    // VkImage m_logoImage;
    // VkDeviceMemory m_logoMemory;
    // VkImageView m_logoView;
    // VkDescriptorSet m_logoDescriptor;
    
    // Content browser
    std::vector<ContentItem> m_contentItems;
    int m_selectedContentIndex;
    std::string m_currentFolder;  // Current folder path (e.g., "Meshes", "Materials")
    std::vector<std::string> m_folders;  // List of folders
    int m_selectedFolderIndex;
    bool m_showMaterialNamePopup;
    char m_newMaterialName[128];
    bool m_showObjectNamePopup;
    char m_newObjectName[128];
    
    // Rename functionality
    bool m_showRenamePopup;
    char m_renameBuffer[128];
    int m_renamingItemIndex;
    bool m_renamingFolder;
    int m_renamingFolderIndex;
    
    // Drag preview state
    bool m_isDraggingObject;
    int m_draggedObjectIndex;
    glm::vec3 m_dragPreviewPosition;
    
    // Undo/Redo system
    std::vector<UndoState> m_undoStack;
    std::vector<UndoState> m_redoStack;
    
    // Rotation logging
    std::ofstream m_rotationLogFile;
    bool m_isLoggingRotation;
    int m_rotationLogFrameCount;
    
    // Project state
    std::string m_currentProjectPath;
    bool m_projectModified;
    std::set<int> m_unsavedContentItems;  // Track which content items have unsaved changes (by index)
    
    // Project name dialog
    bool m_showProjectNameDialog;
    char m_projectNameBuffer[128];
    bool m_isNewProject; // true for new, false for save-as
    
    // Startup project dialog
    bool m_showStartupDialog;
    bool m_engineInitialized;
    
    // Exposure settings
    bool m_showPreferencesWindow;
    bool m_autoExposure;
    float m_exposure;
    float m_targetExposure;
    float m_exposureAdaptSpeed;
    
    // Selection outline settings
    glm::vec3 m_outlineColor;
    int m_outlineThickness;
    
    // UI scaling preference. In auto mode the scale follows the monitor's OS display scale;
    // otherwise m_uiScalePref (1.0 = 100%) is used. Changes are applied at the start of the next frame.
    bool m_uiScaleAuto = true;
    float m_uiScalePref = 1.0f;
    bool m_uiScaleDirty = false;

    void renderPreferencesWindow();
    void loadPreferences();
    void savePreferences() const;
    float calculateSceneLuminance();
    
    // Texture cache for material textures
    struct CachedTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        int width = 0;
        int height = 0;
    };
    std::unordered_map<std::string, CachedTexture> m_textureCache;
    
    // Material evaluation result including texture info for PBR
    struct MaterialRenderInfo {
        glm::vec3 baseColor = glm::vec3(0.8f);
        glm::vec3 emissiveColor = glm::vec3(0.0f);
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        
        // Texture strength values
        float metallicStrength = 1.0f;
        float roughnessStrength = 1.0f;
        float normalStrength = 1.0f;
        float aoStrength = 1.0f;
        float emissiveStrength = 1.0f;
        float heightStrength = 0.5f;  // Parallax height scale (0.0-1.0, scaled down in shader)
        
        // Texture flags
        bool useAlbedoTexture = false;
        bool useMetallicTexture = false;
        bool useRoughnessTexture = false;
        bool useNormalTexture = false;
        bool useAOTexture = false;
        bool useEmissiveTexture = false;
        bool useHeightTexture = false;
        
        // Texture invert flags
        bool invertMetallic = false;
        bool invertRoughness = false;
        bool invertNormal = false;
        bool invertAO = false;
        bool invertHeight = false;
        
        // Texture paths
        std::string albedoTexturePath;
        std::string metallicTexturePath;
        std::string roughnessTexturePath;
        std::string normalTexturePath;
        std::string aoTexturePath;
        std::string emissiveTexturePath;
        std::string heightTexturePath;
    };
    MaterialRenderInfo getMaterialRenderInfo(const SceneObject& obj) const;
    CachedTexture* getOrLoadTexture(const std::string& path);
    void cleanupTextureCache();
    
    void markProjectModified();
    void renderMenuBar();
    void renderProjectNameDialog();
    void renderStartupDialog();
    void renderExitConfirmationDialog();
    void renderTabCloseConfirmationDialog();
    void onWindowClose();
    
    // Exit confirmation state
    bool m_showExitConfirmation;
    bool m_exitConfirmed;
    
    // Tab close confirmation state
    bool m_showTabCloseConfirmation;
    int m_tabToClose;  // Index of tab pending close confirmation
};

} // namespace Forge
