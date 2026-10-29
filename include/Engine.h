#pragma once

#include "core/Window.h"
#include "core/Camera.h"
#include "core/CameraSettings.h"
#include "core/SpringArm.h"
#include "core/CameraFrustum.h"
#include "core/CollisionSettings.h"
#include "core/LightSettings.h"
#include "core/EnvironmentSettings.h"
#include "physics/CollisionOutline.h"
#include "physics/PlayPhysics.h"
#include "rendering/Shader.h"
#include "rendering/Mesh.h"
#include "rendering/VulkanContext.h"
#include "ui/GizmoTypes.h"
#include "ui/TabSystem.h"
#include "ui/ContentItem.h"
#include "rendering/WorldGrid.h"
#include "rendering/SceneLighting.h"
#include "rendering/LightingData.h"
#include "rendering/ShadowRenderer.h"
#include "ui/OutputLogPanel.h"
#include "rendering/AmbientOcclusion.h"
#include "rendering/RayTracing.h"
#include "rendering/RayDebugPass.h"
#include "rendering/TextureTable.h"
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
#include <optional>
#include <future>
#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <ctime>

namespace Propulsion {

struct EvaluatedMaterial;  // what a material's node graph works out (nodegraph/NodeGraph.h)

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
    std::string inputMappingId;    // Runtime only, never saved: filled in when Play starts from the source Object asset (empty = no input)
    bool collisionEnabled = true;  // Runtime only: `make Collision.Enabled false` in a script turns this off (and a destroyed object's is off)
    float lookPitch = 0.0f;    // Runtime only: while playing, the pitch (degrees) that tilts only this object's camera (see SpringArmSettings::usePawnControlRotation)
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
    
    // A step is a move (the fields above), an object being added to the scene (created, duplicated) or removed from it, or an
    // object being edited (its name, visibility, mesh or material). For the last three, `object` is the object (so that it can
    // be put back, or taken out again; for an edit, the object as it was before), `objectIndex` is where it is in the scene, and
    // `selectionBefore` is what was selected before. The steps are undone in the reverse of the order they were made in, so the
    // scene is always just as the step saw it. See core/SceneHistory.h.
    enum class Kind { Move, Added, Removed, Edited };
    Kind kind = Kind::Move;
    std::shared_ptr<SceneObject> object;
    int selectionBefore = -1;

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
    void openProject(const std::string& filepath);  // skip the startup dialog and open this .pproj (also: PropulsionMKI.exe <project.pproj>)
    void openScript(const std::string& scriptPath) { openScriptGraphTab(scriptPath); }  // after openProject; PropulsionMKI.exe <project.pproj> <Scripts/x.prop>: open its graph
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
    
    // Play mode: runs PropulsionScript scripts on a copy of the scene, then restores the scene on stop.
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
    void recordObjectEdit(int index, const SceneObject& before);  // the object at `index` was just edited; Ctrl+Z puts `before` back
    void duplicateObject(int index);  // a copy in the same place as the original (Ctrl+D, Edit and the Objects panel menu); the copy is selected, and Ctrl+Z removes it

    // Object assets are the defaults for the instances placed from them.
    const ContentItem* sourceAssetOf(const SceneObject& obj) const;
    void applyAssetDefaults(SceneObject& obj) const;   // copies the asset's mesh/material into every property not overridden
    void syncInstancesOf(const std::string& assetGUID); // after an asset's defaults change
    void inferOverrides(SceneObject& obj) const;        // for a project saved before overrides were recorded

    // The properties of an Object asset, drawn by the Object editor and by the Graph view. Each returns true if the
    // user changed something (the caller keeps it: the Object editor waits for Ctrl+S, the Graph view saves).
    bool drawObjectMeshProperty(ContentItem& item);
    bool drawObjectMaterialProperty(ContentItem& item);
    bool drawObjectScriptProperty(ContentItem& item, bool inGraphView);
    bool drawAddComponentButton(ContentItem& item);   // "+ Add Component" and its menu
    bool drawObjectCameraProperty(ContentItem& item); // the Camera component's settings, with Remove
    bool drawObjectLightProperty(ContentItem& item);  // the Light component's settings, with Remove

    // Environment (ui/EnvironmentPanel.cpp)
    struct EnvironmentLoad {
        std::string hdriPath;  // what was asked for ("" is the default sky)
        bool ok = false;
        std::string error;
        std::string info;      // for the log: its size, and how long preparing it took
        Environment::Prepared prepared;
    };
    void updateEnvironment();                         // before a frame is recorded: swap in a finished HDRI, start loading a new one
    void startEnvironmentLoad();
    static EnvironmentLoad loadEnvironment(const std::string& hdriPath, const std::string& fullPath);  // runs on another thread
    void updateLightingBlocks(uint32_t frame);        // before a frame is recorded: the scene's lights and sky for this frame
    void renderEnvironmentSection();                  // the Environment panel's controls (in the Objects panel)
    bool drawMaterialSurfaceSettings(ContentItem& item);  // the Material editor's blend mode and what see-through does; true if changed
    bool drawRenderModeSelector();                    // the render mode picker over the viewport's corner; true while the mouse is on it
    RenderMode activeRenderMode() const;              // what the viewport is drawn with: the project's mode, unless this GPU can't do it
    void refreshProjectHdris();
    void useHdriAsEnvironment(const ContentItem& item);
    VkDescriptorSet hdriThumbnail(const std::string& fullPath);
    // The editor's picture of a light (never drawn while playing), at `place` (the light's position and turn, no scale). Uses the
    // next uniform block of the offscreen shader, `drawIndex`, which it advances.
    void drawLightIcon(VkCommandBuffer commandBuffer, const LightSettings& light, const glm::mat4& place, const glm::mat4& view,
                       const glm::mat4& projection, const glm::vec3& cameraPos, uint32_t frame, uint32_t& drawIndex);
    bool drawObjectSpringArmProperty(ContentItem& item); // the Spring Arm component's settings, with Remove
    bool drawObjectCharacterProperty(ContentItem& item); // the Character Movement component's settings, with Remove
    bool drawObjectCollisionProperty(ContentItem& item); // whether the Object is solid
    bool drawObjectInputProperty(ContentItem& item);     // the Input Mapping the Object is played with

    // Input as assets (Enhanced-Input style; see ui/InputAssets.cpp): Input Actions, and Input Mappings (tables of keys).
    void ensureDefaultInput();                                                // a project with no Input Actions gets the defaults
    void createInputAsset(const std::string& type, const std::string& name);  // type is "InputAction" or "InputMapping"
    bool saveInputAsset(ContentItem& item);                                   // write its file now
    void syncScriptInputs();                                                  // tell the script checker which Input Actions exist
    void renderInputAssetEditorTab(Tab& tab);                                 // the editor for either
    void fitCharacterToMesh(ContentItem& item) const;    // size the capsule to the Object's mesh
    bool drawObjectComponents(ContentItem& item);     // the Object editor's list of components
    bool saveObjectAsset(ContentItem& item);          // write its .pobj file now
    void retargetMeshReferences(const std::string& oldId, const std::string& newId);  // after a mesh's ID changes (it was renamed)

    // Camera component: the Object asset an instance came from says whether it has one.
    bool cameraOf(const SceneObject& obj, CameraSettings& out) const;                  // false if the object has no camera
    bool lightOf(const SceneObject& obj, LightSettings& out) const;                    // false if the object has no light
    std::vector<SceneLight> collectSceneLights() const;                                // every visible object that shines, where it shines from
    glm::mat4 cameraWorldMatrix(const SceneObject& obj, const CameraSettings& cam) const; // where the camera is (no scale)
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
    std::optional<Camera> m_editorCameraBackup;   // the editor's view while Play looks through a Camera component
    std::unique_ptr<Shader> m_shader;              // Main shader for swap chain rendering
    std::unique_ptr<Shader> m_offscreenShader;     // Shader for offscreen rendering
    std::unique_ptr<Shader> m_maskShader;          // Shader for selection mask rendering
    std::unique_ptr<Shader> m_gizmoShader;
    std::unique_ptr<Mesh> m_cube;
    std::unique_ptr<Mesh> m_sphere;
    std::unique_ptr<Mesh> m_cylinder;
    std::unique_ptr<Mesh> m_cameraMesh;   // editor-only picture of a Camera component (resources/default_meshes/Camera.fbx)
    
    // Gizmo-specific meshes (procedurally generated for consistent unit size)
    std::unique_ptr<Mesh> m_gizmoCube;
    std::unique_ptr<Mesh> m_gizmoCone;
    std::unique_ptr<Mesh> m_gizmoTorus;
    std::unique_ptr<Mesh> m_gizmoDisc;
    std::unique_ptr<WorldGrid> m_worldGrid;
    std::unique_ptr<ThumbnailGenerator> m_thumbnailGenerator;

    // Lights and environment (rendering/SceneLighting.h). The scene's sky belongs to the project; an HDRI is read and
    // prepared on another thread and swapped in between frames, so picking one never freezes the editor.
    std::unique_ptr<SceneLighting> m_lighting;
    std::unique_ptr<ShadowRenderer> m_shadows;                 // the depth maps from the lights' points of view
    std::unique_ptr<AmbientOcclusion> m_ambientOcclusion;      // depth + normals pre-pass, then occlusion from them
    std::unique_ptr<TextureTable> m_textureTable;              // every material texture in one array the shaders index
    std::unique_ptr<RayTracing> m_rayTracing;                  // acceleration structures for the scene (null if the GPU has no ray queries)
    std::unique_ptr<RayDebugPass> m_rayDebug;                  // primary rays drawn over the viewport, to check the above
    int m_rayDebugView = 0;                                    // 0 off, 1 hit normals, 2 per-object colours; a developer view, not saved
    ShadowFrame m_shadowFrame;                                 // this frame's shadow matrices (worked out before it is recorded)
    std::vector<ShadowRenderer::Caster> m_shadowCasters;       // what casts them this frame
    // One mesh drawn in the scene pass: its per-object uniforms (written before any pass is recorded) are block `block`.
    struct SceneDraw {
        Mesh* mesh = nullptr;
        glm::mat4 model{1.0f};
        uint32_t block = 0;
        BlendMode blendMode = BlendMode::Opaque;
        bool twoSided = false;
    };
    // The scene shader's pipelines (Shader::addVariant numbers; 0 is the normal one): see-through surfaces are blended, and Hybrid RT has its own
    int m_pipelineBlend = 0;
    int m_pipelineHybrid = 0;
    int m_pipelineHybridBlend = 0;
    EnvironmentSettings m_environment;
    std::future<EnvironmentLoad> m_environmentLoad;   // valid while a load is running
    std::string m_loadedHdriPath;                     // what the GPU image shows right now
    std::string m_failedHdriPath;                     // the last one that couldn't be read (not retried until it changes)
    std::string m_environmentStatus;                  // shown under the picker: "Loading...", or why it failed
    Environment::Image m_environmentSmall;            // a small copy of the sky, to work out its diffuse lighting from
    Environment::Sh9 m_environmentSh;
    float m_environmentShYaw = 0.0f;                  // the rotation m_environmentSh was worked out for
    bool m_environmentShDirty = true;
    bool m_warnedTooManyLights = false;
    std::vector<std::string> m_projectHdris;          // the HDRIs in the project (relative paths), for the picker
    std::unordered_map<std::string, VkDescriptorSet> m_hdriThumbnails;  // by file, so a rescan doesn't decode every HDRI again
    
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
    bool m_showCollisionShapes = false;  // View > Collision Shapes: the solid shapes drawn over the meshes (editor only, hidden in Play)
    bool m_showObjectsPanel;
    bool m_showContentBrowser;
    bool m_showWorldGrid;
    bool m_firstFrame;
    bool m_dockingLayoutInitialized;
    ImVec2 m_lastDockSpaceSize;
    
    // Output Log (View -> Output Log): everything the engine, the script compiler and running scripts log
    OutputLogPanel m_outputLog;
    bool m_showOutputLog = true;
    void generateThumbnail(ContentItem& item, Mesh* mesh);
    void generateMaterialThumbnail(ContentItem& item, const glm::vec3& color);
    void generateMaterialThumbnailWithTexture(ContentItem& item, const glm::vec3& color, bool useTexture, const std::string& texturePath);
    void createMaterial(const std::string& name);
    void createObject(const std::string& name, int lightType = -1);  // lightType: 0 a directional light, 1 a point light, -1 none
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

    // What the physics knows about each scene object while Play runs (built when it starts; same order as m_sceneObjects).
    std::vector<Physics::Body> m_playBodies;
    SpringArmState m_playArm;                        // the Play camera's spring arm as it was last frame (what its lag eases from)
    Physics::Contacts m_playContacts;                // which scripted objects are touching or blocking each other
    std::vector<Physics::Pose> playPoses() const;    // where every scene object is right now, for the physics

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
    int m_newObjectLightType = -1;  // the Object being named is a light of this type (see createObject)
    bool m_showInputActionNamePopup = false;
    bool m_showInputMappingNamePopup = false;
    char m_newInputActionName[128] = {};
    char m_newInputMappingName[128] = {};
    
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
    // A name being typed in the Objects panel: one history step for the whole edit, made when the text field is let go of.
    struct PendingEdit {
        unsigned int fieldId = 0;  // the ImGui id of the text field (0: none)
        int index = -1;
        std::shared_ptr<SceneObject> before;
    } m_pendingNameEdit;
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
        VkImageView view = VK_NULL_HANDLE;       // the picture decoded from sRGB: for colours (base colour, emissive)
        VkImageView dataView = VK_NULL_HANDLE;   // the same picture as stored: for data (metallic, roughness, normals, occlusion, opacity)
        VkSampler sampler = VK_NULL_HANDLE;
        int32_t slotColor = -1;                  // its places in the texture table (-1: not in it)
        int32_t slotData = -1;
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
        bool useOpacityTexture = false;
        
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
        std::string opacityTexturePath;  // Masked / Translucent: its red channel is the alpha (else the base colour texture's alpha)

        MaterialSettings settings;       // solid, cut out or see-through
    };
    MaterialRenderInfo getMaterialRenderInfo(const SceneObject& obj) const;
    static MaterialRenderInfo materialRenderInfoFrom(const EvaluatedMaterial& evaluated, const MaterialSettings& settings);

    // Drawing an Object the way the level does, for the Object editor's preview.
    Mesh* meshByGUID(const std::string& guid) const;                   // the built-in mesh a Mesh asset stands for (Cube if unknown)
    SceneObject proxyObjectOf(const ContentItem& objectAsset) const;   // a stand-in scene object with the asset's mesh/material
    void loadMaterialTextures(const MaterialRenderInfo& info);         // before beginFrame: load what a material uses and put it in the texture table
    UniformBufferObject makeMaterialUbo(const MaterialRenderInfo& info, const glm::mat4& model, const glm::mat4& view,
                                        const glm::mat4& projection, const glm::vec3& cameraPos);
    void assetPreviewMatrices(const Tab& tab, glm::mat4& view, glm::mat4& projection, glm::vec3& cameraPos) const;

    // Camera component outline, shared by the level viewport and the Object editor preview.
    const SceneObject* playCameraOwner(CameraSettings& cam) const;  // the object whose camera Play looks through, or null
    InputSetup inputSetup() const;                                   // the project's Input Actions and Input Mappings, gathered from its assets
    Physics::Body physicsBodyOf(const SceneObject& obj) const;       // the shape and character settings of one object, as the physics sees it
    void buildPlayBodies();                                          // when Play starts: the shape and character settings of every object
    void drawCollisionShapes(const glm::mat4& view, const glm::mat4& projection, ImVec2 pos, ImVec2 size);  // View > Collision Shapes
    void stepPlayPhysics(float deltaTime);                           // after the scripts: gravity, jumping, and characters stopping at solid objects
    void applyLookPitch();                                           // move a scripted object's pitch onto its camera, where its Camera component asks for that
    void updatePlayCamera(float deltaTime = 0.0f);                   // point the viewport camera at it (every frame of Play; `deltaTime` is the game time that passed, for the arm's lag)
    float playAspect() const;   // the shape of what Play shows: the level viewport's, or 16:9 before it has one
    void drawCameraFrustum(const CameraSettings& cam, const glm::mat4& cameraToWorld, const glm::mat4& view,
                           const glm::mat4& projection, ImVec2 pos, ImVec2 size);
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

} // namespace Propulsion
