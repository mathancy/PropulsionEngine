/**
 * NodeGraphSelection.cpp
 *
 * Selecting several nodes (click, Ctrl/Shift+click, a box dragged on empty canvas, Ctrl+A), deleting
 * them together, and undo/redo for material graphs.
 *
 * Undo works on the graph as a whole: every frame the graph is reduced to a fingerprint (which nodes,
 * where, what they hold, which wires). When it changes and the interaction making the change is over
 * (mouse released, text field left), the graph as it was before is kept as one undo step. That makes
 * every kind of change undoable the same way (a drag, dragging many nodes, deleting many nodes, a
 * wire, a color or a texture choice) without each place that changes something having to say so.
 *
 * Part of the NodeGraph system.
 */

#include "nodegraph/NodeGraph.h"

#include <algorithm>

namespace Propulsion {

// ---------------------------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------------------------

std::vector<int> NodeGraph::selectedNodeIds() const {
    std::vector<int> ids;
    for (const auto& n : m_nodes) {
        if (n->selected) ids.push_back(n->id);
    }
    return ids;
}

int NodeGraph::selectedCount() const {
    int count = 0;
    for (const auto& n : m_nodes) {
        if (n->selected) count++;
    }
    return count;
}

void NodeGraph::selectAll() {
    for (auto& n : m_nodes) n->selected = true;
    if (!getNode(m_selectedNodeId) && !m_nodes.empty()) m_selectedNodeId = m_nodes.back()->id;
}

void NodeGraph::clearSelection() {
    for (auto& n : m_nodes) n->selected = false;
    m_selectedNodeId = -1;
}

std::vector<ScriptNode*> NodeGraph::selectedScriptNodes() const {
    std::vector<ScriptNode*> out;
    for (const auto& n : m_nodes) {
        if (!n->selected) continue;
        if (ScriptNode* s = dynamic_cast<ScriptNode*>(n.get())) out.push_back(s);
    }
    return out;
}

void NodeGraph::deleteSelected() {
    if (m_hasScriptNodes) {
        // The script text is changed by the host (in one step, however many nodes), and the graph
        // is rebuilt from it.
        std::vector<ScriptNode*> doomed = selectedScriptNodes();
        clearSelection();
        if (!doomed.empty() && m_scriptHooks.onDeleteNodes) m_scriptHooks.onDeleteNodes(doomed);
        return;
    }
    std::vector<int> doomed;
    for (const auto& n : m_nodes) {
        if (n->selected && n->canDelete) doomed.push_back(n->id);
    }
    for (int id : doomed) removeNode(id);
    if (!doomed.empty()) m_hasUnsavedChanges = true;
    // What can't be deleted (the output node) stays, selected or not.
    m_selectedNodeId = -1;
    for (const auto& n : m_nodes) {
        if (n->selected) m_selectedNodeId = n->id;
    }
}

void NodeGraph::duplicateSelected() {
    if (!m_hasScriptNodes) return;
    const std::vector<ScriptNode*> nodes = selectedScriptNodes();
    if (!nodes.empty() && m_scriptHooks.onDuplicateNodes) m_scriptHooks.onDuplicateNodes(nodes);
}

void NodeGraph::drawBoxSelect(ImDrawList* drawList) {
    if (!m_isBoxSelecting) return;
    const ImVec2 lo(std::min(m_boxStart.x, m_boxEnd.x), std::min(m_boxStart.y, m_boxEnd.y));
    const ImVec2 hi(std::max(m_boxStart.x, m_boxEnd.x), std::max(m_boxStart.y, m_boxEnd.y));
    drawList->AddRectFilled(lo, hi, IM_COL32(255, 200, 100, 28));
    drawList->AddRect(lo, hi, IM_COL32(255, 200, 100, 200), 0.0f, 0, 1.0f);
}

// ---------------------------------------------------------------------------------------------
// Undo and redo
// ---------------------------------------------------------------------------------------------

static std::unique_ptr<Node> cloneNode(const Node* node) {
    if (auto* c = dynamic_cast<const ColorWheelNode*>(node)) return std::make_unique<ColorWheelNode>(*c);
    if (auto* i = dynamic_cast<const ImageNode*>(node)) return std::make_unique<ImageNode>(*i);
    if (auto* o = dynamic_cast<const MaterialOutputNode*>(node)) return std::make_unique<MaterialOutputNode>(*o);
    if (auto* s = dynamic_cast<const ScriptNode*>(node)) return std::make_unique<ScriptNode>(*s);
    return std::make_unique<Node>(*node);
}

NodeGraph::Snapshot NodeGraph::takeSnapshot() const {
    Snapshot snapshot;
    snapshot.nodes.reserve(m_nodes.size());
    for (const auto& n : m_nodes) snapshot.nodes.push_back(cloneNode(n.get()));
    snapshot.connections = m_connections;
    return snapshot;
}

void NodeGraph::restoreSnapshot(const Snapshot& snapshot) {
    m_nodes.clear();
    for (const auto& n : snapshot.nodes) m_nodes.push_back(cloneNode(n.get()));
    m_connections = snapshot.connections;

    m_selectedNodeId = -1;
    for (const auto& n : m_nodes) {
        if (n->selected) m_selectedNodeId = n->id;
    }
    m_hoveredNodeId = -1;
    m_hoveredPinNodeId = -1;
    m_hoveredPinId = -1;
    m_draggingNodeId = -1;
    m_isDraggingNode = false;
    m_isDraggingPin = false;
    m_isDraggingHue = false;
    m_isDraggingSV = false;
    m_isBoxSelecting = false;
    m_dragStart.clear();
    m_editingField = EditingField::None;
}

/// Undo and redo leave the selection as it was (for the nodes that still exist).
void NodeGraph::keepSelection(const std::vector<int>& ids) {
    clearSelection();
    for (int id : ids) {
        if (Node* n = getNode(id)) {
            n->selected = true;
            m_selectedNodeId = id;
        }
    }
}

/// A hash of everything undo cares about: which nodes there are, where, what they hold, and the wires.
uint64_t NodeGraph::fingerprint() const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* data, size_t size) {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; i++) {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
    };
    auto mixString = [&](const std::string& text) {
        mix(text.data(), text.size());
        mix("|", 1);
    };
    for (const auto& n : m_nodes) {
        mix(&n->id, sizeof(n->id));
        mixString(n->type);
        mix(&n->position, sizeof(n->position));
        if (auto* c = dynamic_cast<const ColorWheelNode*>(n.get())) {
            mix(&c->color, sizeof(c->color));
            mix(&c->hue, sizeof(c->hue));
            mix(&c->saturation, sizeof(c->saturation));
            mix(&c->value, sizeof(c->value));
        } else if (auto* i = dynamic_cast<const ImageNode*>(n.get())) {
            mixString(i->selectedTexturePath);
            mix(&i->textureType, sizeof(i->textureType));
            mix(&i->strength, sizeof(i->strength));
            mix(&i->invertTexture, sizeof(i->invertTexture));
        }
    }
    for (const auto& c : m_connections) {
        mix(&c, sizeof(c));
    }
    return h;
}

/// Called every frame. Notices when the graph changed and, once the interaction that changed it is
/// over, records the graph as it was before as one undo step.
void NodeGraph::trackUndo() {
    if (m_hasScriptNodes) return;
    const uint64_t now = fingerprint();
    if (!m_undoReady) {
        m_stable = takeSnapshot();
        m_stableFingerprint = now;
        m_undoReady = true;
        return;
    }
    if (now == m_stableFingerprint) return;

    ImGuiIO& io = ImGui::GetIO();
    const bool busy = io.MouseDown[0] || m_isDraggingNode || m_isDraggingPin || m_isDraggingHue || m_isDraggingSV ||
                      m_isBoxSelecting || io.WantTextInput;
    if (busy) return;

    m_undoStack.push_back(std::move(m_stable));
    if (m_undoStack.size() > 100) m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
    m_stable = takeSnapshot();
    m_stableFingerprint = now;
}

bool NodeGraph::undo() {
    if (m_hasScriptNodes || !m_undoReady) return false;
    // A change that is still being made (Ctrl+Z pressed mid-edit) becomes a step of its own first.
    if (fingerprint() != m_stableFingerprint) {
        m_undoStack.push_back(std::move(m_stable));
        m_stable = takeSnapshot();
        m_stableFingerprint = fingerprint();
        m_redoStack.clear();
    }
    if (m_undoStack.empty()) return false;
    m_redoStack.push_back(takeSnapshot());
    Snapshot previous = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    const std::vector<int> selection = selectedNodeIds();
    restoreSnapshot(previous);
    keepSelection(selection);
    m_stable = std::move(previous);
    m_stableFingerprint = fingerprint();
    m_hasUnsavedChanges = true;
    return true;
}

bool NodeGraph::redo() {
    if (m_hasScriptNodes || !m_undoReady || m_redoStack.empty()) return false;
    m_undoStack.push_back(takeSnapshot());
    Snapshot next = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    const std::vector<int> selection = selectedNodeIds();
    restoreSnapshot(next);
    keepSelection(selection);
    m_stable = std::move(next);
    m_stableFingerprint = fingerprint();
    m_hasUnsavedChanges = true;
    return true;
}

} // namespace Propulsion
