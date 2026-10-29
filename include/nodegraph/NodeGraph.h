#pragma once

// Node Graph System
// Visual node-based material editor

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>

#include "nodes/NodeTypes.h"
#include "nodes/MaterialOutputNode.h"
#include "nodes/ImageNode.h"
#include "nodes/ColorWheelNode.h"
#include "nodes/ScriptNode.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>

namespace Propulsion {

/// Where the user right-clicked, or let go of a wire, in a script graph.
struct ScriptMenuContext {
    enum class Kind {
        Empty,     // empty canvas
        Node,      // on a node
        Pin,       // on an input pin (to compute its value with another node)
        ExecDrop,  // let go of an execution wire on empty canvas (to add a node there)
        GetterDrop // let go of a Get node's wire on empty canvas (to pick a pin to plug the variable into)
    };
    Kind kind = Kind::Empty;
    int nodeId = -1;
    int pinId = -1;
    /// Where on the canvas it happened, in grid units: where a node added from the menu goes.
    ImVec2 spawn = ImVec2(0, 0);
};

/// One thing the open menu can add, for typing to find it. Picking it does what choosing it in the menu does.
struct ScriptSearchResult {
    std::string label;     // what it is called
    std::string category;  // shown beside it, and searched
    std::string keywords;  // searched, not shown (how the node reads in the script)
    std::string tooltip;
    std::function<void()> pick;
};

/// What a script graph needs from its host. A script graph never changes itself: the user's actions
/// are reported here, the host changes the script text, and the graph is rebuilt from the result.
struct ScriptGraphHooks {
    /// Everything the menu for this context can add (empty for a menu that adds nothing, which then can't be
    /// searched). Asked once each time the menu opens, when the user starts typing.
    std::function<std::vector<ScriptSearchResult>(const ScriptMenuContext&)> searchResults;
    /// A text field was edited (or a choice picked) and the edit confirmed.
    std::function<void(ScriptNode&, ScriptPinField&, const std::string& text)> onPinEdited;
    /// An execution wire was dragged from an output pin of `from` to the input pin of `to`.
    std::function<void(ScriptNode& from, int fromPinId, ScriptNode& to)> onConnectExec;
    /// The user pressed Delete with script nodes selected (one or many).
    std::function<void(const std::vector<ScriptNode*>&)> onDeleteNodes;
    /// The user pressed Ctrl+D with script nodes selected (one or many).
    std::function<void(const std::vector<ScriptNode*>&)> onDuplicateNodes;
    /// The user finished dragging nodes (one or many); each node's `position` is where it was dropped.
    std::function<void(const std::vector<ScriptNode*>&)> onNodesMoved;
    /// Draw the items of the context menu (called inside the open popup).
    std::function<void(const ScriptMenuContext&)> drawMenu;
    /// A node was clicked (selected).
    std::function<void(ScriptNode*)> onSelected;
    /// A Get node's output was wired to a value pin of `to`.
    std::function<void(ScriptNode& getter, ScriptNode& to, int toPinId)> onConnectGetter;
    /// The output of a value node that is plugged into nothing was wired to a value pin of `to`.
    std::function<void(ScriptNode& value, ScriptNode& to, int toPinId)> onConnectValue;
    /// A variable (its name) was dragged from the Variables panel and dropped on a node's field.
    std::function<void(ScriptNode& node, ScriptPinField& field, const std::string& variable)> onDropVariableOnPin;
    /// ...or dropped on empty canvas, at this grid position.
    std::function<void(const std::string& variable, ImVec2 gridPos)> onDropVariable;
    /// "Delete connection" was chosen on the value wire from `from` into the pin `toPinId` of `to`.
    /// (Execution wires can't be cut: a statement has to stay in its chain, so the menu doesn't offer it.)
    std::function<void(ScriptNode& from, ScriptNode& to, int toPinId)> onDeleteConnection;
};

// Forward declarations
struct Node;
struct NodePin;
struct NodeConnection;

// Which field is being edited in ColorWheel node
enum class EditingField {
    None,
    Hex,
    R,
    G,
    B
};

// Result of evaluating a material graph
struct EvaluatedMaterial {
    glm::vec3 baseColor = glm::vec3(0.7f, 0.7f, 0.7f);
    glm::vec3 emissiveColor = glm::vec3(0.0f);
    glm::vec3 subsurfaceColor = glm::vec3(0.0f);
    
    // Base material properties
    float metallic = 0.0f;
    float roughness = 0.5f;
    
    // Texture paths
    std::string albedoTexturePath;
    std::string metallicTexturePath;
    std::string roughnessTexturePath;
    std::string normalTexturePath;
    std::string aoTexturePath;
    std::string emissiveTexturePath;
    std::string heightTexturePath;
    std::string opacityTexturePath;  // the Opacity / Opacity Mask pins: the texture's red channel is the alpha
    
    // Texture usage flags
    bool useAlbedoTexture = false;
    bool useMetallicTexture = false;
    bool useRoughnessTexture = false;
    bool useNormalTexture = false;
    bool useAOTexture = false;
    bool useEmissiveTexture = false;
    bool useHeightTexture = false;
    bool useOpacityTexture = false;
    
    // Texture strengths
    float metallicStrength = 1.0f;
    float roughnessStrength = 1.0f;
    float normalStrength = 1.0f;
    float aoStrength = 1.0f;
    float emissiveTextureStrength = 1.0f;
    float heightStrength = 1.0f;
    
    // Texture invert flags
    bool invertMetallic = false;
    bool invertRoughness = false;
    bool invertNormal = false;
    bool invertAO = false;
    bool invertHeight = false;
};

// Node-based material graph editor
class NodeGraph {
public:
    // Color wheel hit test areas
    enum class ColorWheelHitArea {
        None,
        HueRing,
        SatValSquare
    };

    // Pin hit test result
    struct PinHitResult {
        Node* node = nullptr;
        NodePin* pin = nullptr;
        int nodeId = -1;
        int pinId = -1;
    };

    NodeGraph();
    ~NodeGraph() = default;
    
    // Main render function
    void render(const ImVec2& size);
    
    // Script graphs (see ScriptGraphEditor): hooks, and removing everything while keeping the view
    void setScriptHooks(ScriptGraphHooks hooks) { m_scriptHooks = std::move(hooks); }
    void clear();
    void fitView(const ImVec2& canvasSize);
    /// The node clicked last (or -1): what single-node actions like copy work on.
    int selectedNodeId() const { return m_selectedNodeId; }
    void selectNode(int nodeId);
    /// Scroll the view so this node is in the middle of the canvas.
    void centerOnNode(int nodeId);
    /// Whether the middle of this node is inside the canvas as it was last drawn.
    bool isNodeInView(int nodeId) const;

    // Selection: any number of nodes (click, Ctrl/Shift+click, drag a box on empty canvas, Ctrl+A).
    std::vector<int> selectedNodeIds() const;
    int selectedCount() const;
    void selectAll();
    void clearSelection();
    bool isBoxSelecting() const { return m_isBoxSelecting; }
    /// Delete every selected node (the ones that can be deleted). Script graphs report it to the host.
    void deleteSelected();
    /// Duplicate every selected node. Only script graphs: the host makes the copies (see onDuplicateNodes).
    void duplicateSelected();

    // Undo and redo for material graphs: every change to the nodes, their values and their wires, made
    // by any means, is noticed when the interaction ends (mouse released, field left) and becomes one
    // step; so a drag, a multi-node drag, or a multi-delete is one undo. Script graphs have their own
    // undo (the script text is their source of truth) and don't use this.
    bool undo();
    bool redo();
    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }
    float viewZoom() const { return m_viewZoom; }
    ImVec2 viewOffset() const { return m_viewOffset; }
    float gridSize() const { return m_gridSize; }

    // Node management
    void addNode(std::unique_ptr<Node> node);
    void removeNode(int nodeId);
    Node* getNode(int nodeId);
    const std::vector<std::unique_ptr<Node>>& getNodes() const { return m_nodes; }
    
    // Connection management
    void addConnection(int outNodeId, int outPinId, int inNodeId, int inPinId);
    void removeConnection(int inNodeId, int inPinId);
    const std::vector<NodeConnection>& getConnections() const { return m_connections; }
    
    // View control
    void resetView();
    ImVec2 snapToGrid(const ImVec2& pos) const;
    
    // Coordinate conversion
    ImVec2 screenToGraph(const ImVec2& screenPos, const ImVec2& canvasPos) const;
    ImVec2 graphToScreen(const ImVec2& graphPos, const ImVec2& canvasPos) const;
    
    // ID generation
    int getNextNodeId() { return m_nextNodeId++; }
    int getNextPinId() { return m_nextPinId++; }
    
    // Material evaluation
    EvaluatedMaterial evaluateMaterial() const;
    
    // Texture management
    void setAvailableTextures(const std::vector<TextureInfo>& textures);
    const std::vector<TextureInfo>& getAvailableTextures() const { return m_availableTextures; }
    
    // Change tracking
    bool hasUnsavedChanges() const { return m_hasUnsavedChanges; }
    void markUnsavedChanges() { m_hasUnsavedChanges = true; }
    void clearUnsavedChanges() { m_hasUnsavedChanges = false; }
    
    // Clipboard operations
    void copySelectedNode();
    void pasteNode(const ImVec2& canvasPos, const ImVec2& mousePos);
    void duplicateSelectedNode(const ImVec2& canvasPos);
    
    // Z-order management
    void bringNodeToFront(Node* node);
    std::vector<Node*> getNodesSortedByZOrder() const;
    
    // Hit testing
    PinHitResult hitTestPin(const ImVec2& mousePos, const ImVec2& canvasPos) const;
    ImVec2 getPinScreenPosition(const Node* node, const NodePin& pin, const ImVec2& canvasPos) const;

    /// The corners of the wire drawn between two pins (also what a click on a wire is tested against), from
    /// `startPos` to `endPos`. `startIsOutput` says which end is the output pin. See ConnectionRenderer.cpp for the shapes.
    void electronicPath(const ImVec2& startPos, const ImVec2& endPos, bool startIsOutput, std::vector<ImVec2>& points) const;

    // Node hit testing
    bool isPointInsideNode(const Node* node, const ImVec2& screenPos, const ImVec2& canvasPos) const;
    Node* getTopmostNodeAt(const ImVec2& screenPos, const ImVec2& canvasPos) const;
    
    // ColorWheel hit testing and interaction
    ColorWheelHitArea hitTestColorWheel(ColorWheelNode* node, const ImVec2& mousePos, const ImVec2& canvasPos) const;
    void handleColorWheelInteraction(ColorWheelNode* node, const ImVec2& mousePos, const ImVec2& canvasPos);

private:
    // Rendering helpers
    void renderGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);
    void renderNodeBodies(ImDrawList* drawList, const ImVec2& canvasPos);
    void renderNodePins(ImDrawList* drawList, const ImVec2& canvasPos);
    void renderConnections(ImDrawList* drawList, const ImVec2& canvasPos);
    void renderDraggingConnection(ImDrawList* drawList, const ImVec2& canvasPos);
    void renderContextMenu(const ImVec2& canvasPos);
    
    // Z-ordered rendering
    bool renderNodesInZOrder(ImDrawList* drawList, const ImVec2& canvasPos);
    void renderSingleNodeBody(ImDrawList* drawList, Node* node, const ImVec2& canvasPos);
    void renderSingleNodePins(ImDrawList* drawList, Node* node, const ImVec2& canvasPos);
    
    // ColorWheel node rendering (in ColorWheelRenderer.cpp)
    void renderColorWheelContent(ImDrawList* drawList, ColorWheelNode* node, const ImVec2& contentPos, const ImVec2& contentSize);
    
    // Image node rendering (in ImageNodeRenderer.cpp)
    bool renderSingleImageNodeWidgets(ImageNode* imageNode, const ImVec2& canvasPos);
    bool renderImageNodeWidgets(const ImVec2& canvasPos);
    void renderImageNodeContent(ImDrawList* drawList, ImageNode* node, const ImVec2& contentPos, const ImVec2& contentSize);
    
    // Script node rendering (in ScriptNodeRenderer.cpp)
    void renderScriptNodePins(ImDrawList* drawList, ScriptNode* node, const ImVec2& canvasPos);
    bool renderSingleScriptNodeWidgets(ScriptNode* node, const ImVec2& canvasPos);

    // Connection rendering (in ConnectionRenderer.cpp)
    void drawElectronicConnection(ImDrawList* drawList, const ImVec2& startPos, const ImVec2& endPos, 
                                   ImU32 lineColor, float lineThickness, bool startIsOutput);
    
    void wirePath(const Node* outNode, const NodePin& outPin, const ImVec2& startPos, const ImVec2& endPos, std::vector<ImVec2>& points) const;
    /// Where a connection's two pins are on screen, and the pin it leaves from. False if a node or pin is gone.
    bool connectionEnds(const NodeConnection& conn, const ImVec2& canvasPos, const Node*& outNode, const NodePin*& outPin, ImVec2& start, ImVec2& end);
    /// The connection whose wire passes within a few pixels of `mousePos`, or -1.
    int hitTestConnection(const ImVec2& mousePos, const ImVec2& canvasPos);
    /// Cut a connection (the host does it for a script graph; a material graph is changed here).
    void deleteConnection(const NodeConnection& conn);
    /// The items for a right-click on a wire or a connected pin (inside the open context menu).
    void drawConnectionMenuItems(bool separator);
    /// A dropdown (a combo) inside a node: its list is a window of its own, so it takes the graph's zoom for
    /// its text and a little room above and below, and the node text and the list stay the same size.
    bool beginNodeDropdown(const char* id, const char* preview, ImGuiComboFlags flags = 0);

    // Input handling
    void handleInput(const ImVec2& canvasPos, const ImVec2& canvasSize, bool canvasHovered, bool canvasActive);
    
    // Connection helpers
    bool canConnect(const Node* fromNode, const NodePin& fromPin, const Node* toNode, const NodePin& toPin) const;
    bool isPinConnectable(const Node* node, const NodePin& pin) const;
    
    // Node data
    std::vector<std::unique_ptr<Node>> m_nodes;
    std::vector<NodeConnection> m_connections;
    int m_nextNodeId;
    int m_nextPinId;
    int m_nextZOrder;
    
    // View state
    ImVec2 m_viewOffset;
    float m_viewZoom;   // Effective zoom actually used for drawing/hit-testing = m_userZoom * UI scale
    float m_userZoom;   // What the user controls with the mouse wheel (1.0 = 100%)
    
    // Interaction state
    bool m_isPanning;
    bool m_isDraggingNode;
    ImVec2 m_lastMousePos;
    ImVec2 m_dragOffset;
    ImVec2 m_panStartOffset;
    
    // Selection state (which nodes are selected is `Node::selected`; this is the one clicked last)
    int m_selectedNodeId;
    int m_hoveredNodeId;
    int m_draggingNodeId;

    // Dragging the selection: where each dragged node started, and whether anything moved
    std::map<int, ImVec2> m_dragStart;
    bool m_dragMoved = false;
    bool m_collapseOnRelease = false;  // pressed on a node of a multi-selection: a plain click picks just that node

    // Box select
    bool m_isBoxSelecting = false;
    ImVec2 m_boxStart = ImVec2(0, 0);
    ImVec2 m_boxEnd = ImVec2(0, 0);
    std::set<int> m_boxBase;           // what was already selected when an additive box started
    void drawBoxSelect(ImDrawList* drawList);
    std::vector<ScriptNode*> selectedScriptNodes() const;

    // Undo (material graphs): a snapshot is a copy of every node and wire
    struct Snapshot {
        std::vector<std::unique_ptr<Node>> nodes;
        std::vector<NodeConnection> connections;
    };
    Snapshot takeSnapshot() const;
    void restoreSnapshot(const Snapshot& snapshot);
    void keepSelection(const std::vector<int>& ids);
    uint64_t fingerprint() const;
    void trackUndo();
    std::vector<Snapshot> m_undoStack;
    std::vector<Snapshot> m_redoStack;
    Snapshot m_stable;                 // the graph as of the last quiet moment
    uint64_t m_stableFingerprint = 0;
    bool m_undoReady = false;
    
    // Pin dragging state
    bool m_isDraggingPin;
    int m_draggingPinNodeId;
    int m_draggingPinId;
    bool m_draggingPinIsOutput;
    int m_hoveredPinNodeId;
    int m_hoveredPinId;
    
    // ColorWheel interaction
    bool m_isDraggingHue;
    bool m_isDraggingSV;
    int m_interactingNodeId;
    
    // Field editing
    EditingField m_editingField;
    int m_editingNodeId;
    int m_editingFrameCount;
    
    // Change tracking
    bool m_hasUnsavedChanges;
    
    // Context menu
    bool m_showContextMenu;
    ImVec2 m_contextMenuSpawnPos;
    std::vector<NodeConnection> m_menuConnections;  // what "Delete connection" in the open menu cuts
    bool m_menuWireOnly = false;                     // the menu is only that (a right-click on a wire)

    // Typing in a script graph's menu searches it. The field only exists once something has been typed.
    bool drawMenuSearch();
    char m_menuSearch[128] = {};
    bool m_searchShown = false;     // the field was drawn last frame (so it already has the keyboard)
    bool m_searchCursorToEnd = false;  // the field has just been given the keyboard
    bool m_searchLoaded = false;    // m_searchCandidates is for the menu that is open
    int m_searchSelected = 0;
    std::vector<ScriptSearchResult> m_searchCandidates;

    // Script graphs
    ScriptGraphHooks m_scriptHooks;
    ScriptMenuContext m_scriptMenu;
    bool m_hasScriptNodes = false;  // set while rendering: any ScriptNode present
    ImVec2 m_canvasSize = ImVec2(0, 0);
    
    // Grid settings
    float m_gridSize;
    ImU32 m_gridColorMajor;
    ImU32 m_gridColorMinor;
    ImU32 m_backgroundColor;
    
    // Available textures for Image nodes
    std::vector<TextureInfo> m_availableTextures;
    
    // Image widget interaction flag
    bool m_imageWidgetActive;
    
    // Clipboard for copy/paste
    struct ClipboardData {
        bool hasData = false;
        std::string type;
        ImVec2 size;
        // ColorWheel specific
        glm::vec3 color;
        float hue = 0.0f;
        float saturation = 0.0f;
        float value = 0.0f;
        // Image node specific  
        int selectedTextureIndex = -1;
        TextureType textureType = TextureType::Albedo;
        std::string selectedTexturePath;
        float strength = 1.0f;
        bool invertTexture = false;
    };
    ClipboardData m_clipboard;
};

} // namespace Propulsion
