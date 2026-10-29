#pragma once

#include <string>
#include <vector>
#include <memory>

namespace Propulsion {

// Forward declaration
class NodeGraph;
class ScriptGraphEditor;

enum class TabType {
    Scene,
    AssetEditor,
    DebugOutput,  // retired: the Output Log panel replaced it. Kept so the numbers saved in project files don't shift.
    ScriptGraph   // a PropulsionScript file shown as a node graph (assetId holds its path in the project)
};

// Preview mesh type for material editor viewport
enum class PreviewMeshType {
    Cube = 0,
    Sphere,
    Cylinder
};

struct Tab {
    std::string name;
    TabType type;
    int contentItemIndex; // For asset editor tabs
    std::string assetId; // Store asset ID to maintain reference across folder switches
    unsigned int viewportFBO;
    unsigned int viewportTexture;
    unsigned int viewportRBO;
    int viewportWidth;
    int viewportHeight;
    bool needsSelection; // Flag to trigger ImGui tab selection once
    
    // Camera control for asset editor
    float cameraDistance;
    float cameraYaw;
    float cameraPitch;
    bool hasUnsavedChanges;
    
    // Splitter position for material editor (0.0 to 1.0, default 0.5)
    float splitterRatio;
    
    // Preview mesh type for material editor
    PreviewMeshType previewMeshType;
    
    // Node graph for material editing
    std::shared_ptr<NodeGraph> nodeGraph;
    
    // Node graph for script editing (TabType::ScriptGraph)
    std::shared_ptr<ScriptGraphEditor> scriptGraph;
    
    Tab(const std::string& n, TabType t, int itemIndex = -1)
        : name(n), type(t), contentItemIndex(itemIndex), assetId(""),
          viewportFBO(0), viewportTexture(0), viewportRBO(0),
          viewportWidth(800), viewportHeight(600), needsSelection(true),
          cameraDistance(5.0f), cameraYaw(45.0f), cameraPitch(35.0f), hasUnsavedChanges(false),
          splitterRatio(0.5f), previewMeshType(PreviewMeshType::Cube), nodeGraph(nullptr) {}
};

} // namespace Propulsion
