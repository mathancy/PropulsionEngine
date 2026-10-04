/**
 * NodeGraphClipboard.cpp
 * 
 * Handles clipboard operations for the node graph:
 * - Duplicate selected node (Ctrl+D)
 * - Copy selected node (Ctrl+C)
 * - Paste node at cursor (Ctrl+V)
 * 
 * Part of the NodeGraph system.
 */

#include "nodegraph/NodeGraph.h"
#include <cmath>

namespace Forge {

void NodeGraph::duplicateSelectedNode(const ImVec2& canvasPos) {
    Node* selectedNode = getNode(m_selectedNodeId);
    if (!selectedNode || !selectedNode->canDelete) {
        return;  // Can't duplicate non-deletable nodes (like MaterialOutput)
    }
    
    // Create a new node based on type with offset of 1 grid unit right and down
    ImVec2 newPos = ImVec2(selectedNode->position.x + 1, selectedNode->position.y + 1);
    int newNodeId = m_nextNodeId++;
    
    if (ColorWheelNode* colorNode = dynamic_cast<ColorWheelNode*>(selectedNode)) {
        auto newNode = std::make_unique<ColorWheelNode>(newNodeId, newPos);
        // Copy color data
        newNode->color = colorNode->color;
        newNode->hue = colorNode->hue;
        newNode->saturation = colorNode->saturation;
        newNode->value = colorNode->value;
        newNode->updateHexFromColor();
        newNode->updateRgbBuffersFromColor();
        
        // Select the new node
        selectedNode->selected = false;
        newNode->selected = true;
        m_selectedNodeId = newNodeId;
        
        addNode(std::move(newNode));
    } else if (ImageNode* imageNode = dynamic_cast<ImageNode*>(selectedNode)) {
        auto newNode = std::make_unique<ImageNode>(newNodeId, newPos);
        // Copy image node data
        newNode->selectedTextureIndex = imageNode->selectedTextureIndex;
        newNode->textureType = imageNode->textureType;
        newNode->selectedTexturePath = imageNode->selectedTexturePath;
        newNode->strength = imageNode->strength;
        newNode->invertTexture = imageNode->invertTexture;
        newNode->updateOutputPinType();
        
        // Select the new node
        selectedNode->selected = false;
        newNode->selected = true;
        m_selectedNodeId = newNodeId;
        
        addNode(std::move(newNode));
    }
    
    m_hasUnsavedChanges = true;
}

void NodeGraph::copySelectedNode() {
    Node* selectedNode = getNode(m_selectedNodeId);
    if (!selectedNode || !selectedNode->canDelete) {
        return;  // Can't copy non-deletable nodes
    }
    
    m_clipboard.hasData = false;
    m_clipboard.type = selectedNode->type;
    m_clipboard.size = selectedNode->size;
    
    if (ColorWheelNode* colorNode = dynamic_cast<ColorWheelNode*>(selectedNode)) {
        m_clipboard.color = colorNode->color;
        m_clipboard.hue = colorNode->hue;
        m_clipboard.saturation = colorNode->saturation;
        m_clipboard.value = colorNode->value;
        m_clipboard.hasData = true;
    } else if (ImageNode* imageNode = dynamic_cast<ImageNode*>(selectedNode)) {
        m_clipboard.selectedTextureIndex = imageNode->selectedTextureIndex;
        m_clipboard.textureType = imageNode->textureType;
        m_clipboard.selectedTexturePath = imageNode->selectedTexturePath;
        m_clipboard.strength = imageNode->strength;
        m_clipboard.invertTexture = imageNode->invertTexture;
        m_clipboard.hasData = true;
    }
}

void NodeGraph::pasteNode(const ImVec2& canvasPos, const ImVec2& mousePos) {
    if (!m_clipboard.hasData) {
        return;  // Nothing to paste
    }
    
    // Convert mouse position to graph coordinates and then to grid units
    ImVec2 graphPos = screenToGraph(mousePos, canvasPos);
    float gridX = std::round(graphPos.x / m_gridSize);
    float gridY = std::round(graphPos.y / m_gridSize);
    
    // Center the node on the cursor
    ImVec2 newPos = ImVec2(gridX - m_clipboard.size.x * 0.5f, gridY - m_clipboard.size.y * 0.5f);
    int newNodeId = m_nextNodeId++;
    
    // Deselect all nodes first
    for (auto& n : m_nodes) {
        n->selected = false;
    }
    
    if (m_clipboard.type == "ColorWheel") {
        auto newNode = std::make_unique<ColorWheelNode>(newNodeId, newPos);
        newNode->color = m_clipboard.color;
        newNode->hue = m_clipboard.hue;
        newNode->saturation = m_clipboard.saturation;
        newNode->value = m_clipboard.value;
        newNode->updateHexFromColor();
        newNode->updateRgbBuffersFromColor();
        newNode->selected = true;
        m_selectedNodeId = newNodeId;
        addNode(std::move(newNode));
    } else if (m_clipboard.type == "Image") {
        auto newNode = std::make_unique<ImageNode>(newNodeId, newPos);
        newNode->selectedTextureIndex = m_clipboard.selectedTextureIndex;
        newNode->textureType = m_clipboard.textureType;
        newNode->selectedTexturePath = m_clipboard.selectedTexturePath;
        newNode->strength = m_clipboard.strength;
        newNode->invertTexture = m_clipboard.invertTexture;
        newNode->updateOutputPinType();
        newNode->selected = true;
        m_selectedNodeId = newNodeId;
        addNode(std::move(newNode));
    }
    
    m_hasUnsavedChanges = true;
}

} // namespace Forge
