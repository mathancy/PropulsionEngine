#include "nodegraph/NodeGraph.h"
#include "ui/ImGuiTheme.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace Propulsion {

NodeGraph::NodeGraph()
    : m_nextNodeId(1)
    , m_nextPinId(100)
    , m_nextZOrder(0)
    , m_viewOffset(0.0f, 0.0f)
    , m_viewZoom(1.0f)
    , m_userZoom(1.0f)
    , m_isPanning(false)
    , m_isDraggingNode(false)
    , m_lastMousePos(0.0f, 0.0f)
    , m_dragOffset(0.0f, 0.0f)
    , m_panStartOffset(0.0f, 0.0f)
    , m_selectedNodeId(-1)
    , m_hoveredNodeId(-1)
    , m_draggingNodeId(-1)
    , m_isDraggingPin(false)
    , m_draggingPinNodeId(-1)
    , m_draggingPinId(-1)
    , m_draggingPinIsOutput(false)
    , m_hoveredPinNodeId(-1)
    , m_hoveredPinId(-1)
    , m_isDraggingHue(false)
    , m_isDraggingSV(false)
    , m_interactingNodeId(-1)
    , m_editingField(EditingField::None)
    , m_editingNodeId(-1)
    , m_editingFrameCount(0)
    , m_hasUnsavedChanges(false)
    , m_showContextMenu(false)
    , m_contextMenuSpawnPos(0.0f, 0.0f)
    , m_gridSize(32.0f)
    , m_gridColorMajor(IM_COL32(60, 60, 60, 255))      // Lighter grey for major lines
    , m_gridColorMinor(IM_COL32(45, 45, 45, 255))      // Darker grey for minor lines
    , m_backgroundColor(IM_COL32(30, 30, 30, 255))     // Very dark grey background
    , m_imageWidgetActive(false)
{
}

void NodeGraph::render(const ImVec2& size) {
    // High-DPI: the global UI scale is folded into the effective zoom, so every node
    // dimension (pins, padding, text) scales together via m_viewZoom.
    m_viewZoom = m_userZoom * PropulsionMKI::UI::getUIScale();

    // The node drawing code is authored in unscaled pixels (font->LegacySize, CalcTextSize)
    // and scaled only by m_viewZoom. Neutralize the DPI font scale for the canvas so it
    // doesn't compound with the zoom; it's restored before the context menu and EndChild.
    ImGuiStyle& style = ImGui::GetStyle();
    const float savedFontScaleDpi = style.FontScaleDpi;
    style.FontScaleDpi = 1.0f;

    // Create a child window for the graph canvas
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, m_backgroundColor);
    
    ImGui::BeginChild("NodeGraphCanvas", size, false, 
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
    
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    m_canvasSize = canvasSize;

    m_hasScriptNodes = false;
    for (const auto& n : m_nodes) {
        if (dynamic_cast<const ScriptNode*>(n.get())) {
            m_hasScriptNodes = true;
            break;
        }
    }

    // Get draw list for custom rendering
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Draw background
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), 
                            m_backgroundColor);
    
    // Set up clipping rectangle
    drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);
    
    // Render grid
    renderGrid(drawList, canvasPos, canvasSize);
    
    // Render connections FIRST (behind all nodes)
    renderConnections(drawList, canvasPos);
    
    // Render connection being dragged
    renderDraggingConnection(drawList, canvasPos);
    
    drawList->PopClipRect();
    
    // Render nodes in z-order: each node's body, widgets, and pins together
    // This ensures higher z-order nodes fully occlude lower z-order nodes
    m_imageWidgetActive = renderNodesInZOrder(drawList, canvasPos);
    
    // Add an invisible button to capture all mouse interactions within the canvas
    // This prevents mouse events from leaking to other UI elements (like the viewport)
    // Only capture if we're not interacting with Image node widgets
    ImGui::SetCursorScreenPos(canvasPos);
    bool canvasHovered = false;
    bool canvasActive = false;
    if (!m_imageWidgetActive) {
        ImGui::InvisibleButton("NodeGraphInteractionArea", canvasSize, 
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        canvasHovered = ImGui::IsItemHovered();
        canvasActive = ImGui::IsItemActive();
        // A variable dragged from the Variables panel and let go on empty canvas.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROPULSION_VARIABLE")) {
                if (m_scriptHooks.onDropVariable) {
                    const ImVec2 g = screenToGraph(ImGui::GetMousePos(), canvasPos);
                    m_scriptHooks.onDropVariable(std::string(static_cast<const char*>(payload->Data)), ImVec2(std::round(g.x / m_gridSize), std::round(g.y / m_gridSize)));
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    
    // Handle input (panning, zooming) - pass the hover/active state
    handleInput(canvasPos, canvasSize, canvasHovered, canvasActive);

    // The selection box, over the nodes
    drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);
    drawBoxSelect(drawList);
    drawList->PopClipRect();

    // Notice changes (for undo) once this frame's input has been applied
    trackUndo();
    
    // Render context menu (after input handling, outside clip rect) at the real UI scale
    style.FontScaleDpi = savedFontScaleDpi;
    renderContextMenu(canvasPos);
    
    ImGui::EndChild();
    
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

bool NodeGraph::renderNodesInZOrder(ImDrawList* drawList, const ImVec2& canvasPos) {
    // Render all nodes with proper z-order handling for both visuals AND input.
    // 
    // The solution uses draw list channels to ensure proper layering:
    // - Each node gets 2 channels: one for body, one for widgets
    // - We render in z-order (lowest first) so both visuals AND input work correctly
    // - Channel pairs are: (0,1) for lowest z, (2,3) for next, etc.
    // - Body channels are even, widget channels are odd
    // - This ensures: low body < low widgets < high body < high widgets
    
    bool anyWidgetActive = false;
    
    // Get nodes sorted by z-order (lowest first)
    std::vector<Node*> sortedNodes = getNodesSortedByZOrder();
    int nodeCount = static_cast<int>(sortedNodes.size());
    
    // Get current mouse position to check for occlusion
    ImVec2 mousePos = ImGui::GetMousePos();
    Node* topmostNodeAtMouse = getTopmostNodeAt(mousePos, canvasPos);
    
    // Split into 2 channels per node (body + widgets)
    int totalChannels = nodeCount * 2 + 1;  // +1 for channel 0 as base
    drawList->ChannelsSplit(totalChannels);
    
    // Render all nodes in z-order (lowest first)
    for (int i = 0; i < nodeCount; i++) {
        Node* node = sortedNodes[i];
        int bodyChannel = i * 2;
        int widgetChannel = i * 2 + 1;
        
        // 1. Render node body to body channel
        drawList->ChannelsSetCurrent(bodyChannel);
        renderSingleNodeBody(drawList, node, canvasPos);
        
        // 2. Render ImGui widgets to widget channel (which is above body but below next node's body)
        drawList->ChannelsSetCurrent(widgetChannel);
        
        ImageNode* imageNode = dynamic_cast<ImageNode*>(node);
        if (imageNode) {
            // Check if this node is occluded at the mouse position
            bool isOccludedAtMouse = (topmostNodeAtMouse != nullptr && 
                                      topmostNodeAtMouse != node && 
                                      isPointInsideNode(node, mousePos, canvasPos));
            
            if (!isOccludedAtMouse) {
                if (renderSingleImageNodeWidgets(imageNode, canvasPos)) {
                    anyWidgetActive = true;
                }
            } else {
                // Occluded at mouse - render visually but non-interactive
                ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
                ImGui::BeginDisabled(true);
                renderSingleImageNodeWidgets(imageNode, canvasPos);
                ImGui::EndDisabled();
                ImGui::PopStyleVar();
            }
        }
        
        ScriptNode* scriptNode = dynamic_cast<ScriptNode*>(node);
        if (scriptNode) {
            bool isOccludedAtMouse = (topmostNodeAtMouse != nullptr &&
                                      topmostNodeAtMouse != node &&
                                      isPointInsideNode(node, mousePos, canvasPos));
            if (!isOccludedAtMouse) {
                if (renderSingleScriptNodeWidgets(scriptNode, canvasPos)) {
                    anyWidgetActive = true;
                }
            } else {
                ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
                ImGui::BeginDisabled(true);
                renderSingleScriptNodeWidgets(scriptNode, canvasPos);
                ImGui::EndDisabled();
                ImGui::PopStyleVar();
            }
        }

        // 3. Render pins to widget channel (same layer as widgets, on top of body)
        renderSingleNodePins(drawList, node, canvasPos);
    }
    
    // Merge all channels
    drawList->ChannelsMerge();
    
    return anyWidgetActive;
}

void NodeGraph::renderSingleNodeBody(ImDrawList* drawList, Node* node, const ImVec2& canvasPos) {
    float scaledGridSize = m_gridSize * m_viewZoom;
    float headerHeight = m_gridSize * m_viewZoom;
    
    ImFont* boldFont = ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;
    ImFont* regularFont = ImGui::GetIO().Fonts->Fonts[0];
    float baseBoldSize = boldFont ? boldFont->LegacySize : regularFont->LegacySize;
    
    ColorWheelNode* colorWheelNode = dynamic_cast<ColorWheelNode*>(node);
    ImageNode* imageNode = dynamic_cast<ImageNode*>(node);
    
    // Dynamically calculate node height based on pin count (only for non-special nodes)
    if (!colorWheelNode && !imageNode) {
        size_t maxPins = std::max(node->inputs.size(), node->outputs.size());
        float dynamicHeight = 1.5f + maxPins;
        node->size.y = dynamicHeight;
    }
    
    ImVec2 nodePixelPos = ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize);
    ImVec2 nodeScreenPos = graphToScreen(nodePixelPos, canvasPos);
    ImVec2 nodeSize = ImVec2(node->size.x * scaledGridSize, node->size.y * scaledGridSize);
    ImVec2 nodeEndPos = ImVec2(nodeScreenPos.x + nodeSize.x, nodeScreenPos.y + nodeSize.y);
    
    // Node body background
    ImU32 bodyColor = IM_COL32(40, 40, 45, 245);
    drawList->AddRectFilled(nodeScreenPos, nodeEndPos, bodyColor, 6.0f * m_viewZoom);
    
    // Node header with gradient
    ImVec2 headerEndPos = ImVec2(nodeEndPos.x, nodeScreenPos.y + headerHeight);
    ImU32 hdrColor = node->headerColor;
    int hdrR = (hdrColor >> 0) & 0xFF;
    int hdrG = (hdrColor >> 8) & 0xFF;
    int hdrB = (hdrColor >> 16) & 0xFF;
    int hdrA = (hdrColor >> 24) & 0xFF;
    // A script node that is not wired into the script yet is drawn faded, as a reminder that it makes no code.
    const ScriptNode* scriptNode = dynamic_cast<const ScriptNode*>(node);
    const bool floating = scriptNode && scriptNode->floating;
    if (floating) hdrA = hdrA * 55 / 100;
    
    int leftR = std::min(255, (int)(hdrR * 1.15f));
    int leftG = std::min(255, (int)(hdrG * 0.9f));
    int leftB = std::min(255, (int)(hdrB * 0.9f));
    ImU32 leftColor = IM_COL32(leftR, leftG, leftB, hdrA);
    
    int rightR = std::min(255, hdrR + (255 - hdrR) / 3);
    int rightG = std::min(255, hdrG + (255 - hdrG) / 4);
    int rightB = std::min(255, hdrB + (255 - hdrB) / 4);
    ImU32 rightColor = IM_COL32(rightR, rightG, rightB, hdrA);
    
    drawList->AddRectFilledMultiColor(
        ImVec2(nodeScreenPos.x, nodeScreenPos.y + 3.0f * m_viewZoom),
        headerEndPos, leftColor, rightColor, rightColor, leftColor);
    drawList->AddRectFilled(nodeScreenPos, headerEndPos, leftColor, 6.0f * m_viewZoom, ImDrawFlags_RoundCornersTop);
    drawList->AddRectFilledMultiColor(
        ImVec2(nodeScreenPos.x + 6.0f * m_viewZoom, nodeScreenPos.y),
        ImVec2(nodeEndPos.x - 6.0f * m_viewZoom, headerEndPos.y),
        leftColor, rightColor, rightColor, leftColor);
    
    // Node border (a script node whose line has a problem is outlined in red)
    ImU32 borderColor = node->selected ? IM_COL32(255, 200, 100, 255) : IM_COL32(70, 70, 80, 255);
    float borderThickness = node->selected ? 2.0f * m_viewZoom : 1.0f * m_viewZoom;
    if (const ScriptNode* sn = dynamic_cast<const ScriptNode*>(node)) {
        if (!sn->error.empty() && !node->selected) {
            borderColor = IM_COL32(230, 70, 70, 255);
            borderThickness = 2.0f * m_viewZoom;
        } else if (sn->floating && !node->selected) {
            borderColor = IM_COL32(120, 135, 175, 255);
        }
    }
    drawList->AddRect(nodeScreenPos, nodeEndPos, borderColor, 6.0f * m_viewZoom, 0, borderThickness);

    // Title text
    float headerPadding = 4.0f * m_viewZoom;
    float maxTitleWidth = nodeSize.x - headerPadding * 2;
    float maxTitleHeight = headerHeight - headerPadding * 2;
    ImVec2 baseTitleSize = ImGui::CalcTextSize(node->name.c_str());
    float scaleToFitWidth = maxTitleWidth / baseTitleSize.x;
    float scaleToFitHeight = maxTitleHeight / baseTitleSize.y;
    float titleScale = std::max(0.1f, std::min(2.0f, std::min(scaleToFitWidth, scaleToFitHeight)));
    ImVec2 titleSize = ImVec2(baseTitleSize.x * titleScale, baseTitleSize.y * titleScale);
    ImVec2 titlePos = ImVec2(
        nodeScreenPos.x + (nodeSize.x - titleSize.x) * 0.5f,
        nodeScreenPos.y + (headerHeight - titleSize.y) * 0.5f);
    ImFont* titleFont = boldFont ? boldFont : regularFont;
    float titleFontSize = baseBoldSize * titleScale;
    if (titleFontSize > 1.0f) {
        drawList->AddText(titleFont, titleFontSize, titlePos, IM_COL32(255, 255, 255, 255), node->name.c_str());
    }
    
    // Custom node content
    if (colorWheelNode) {
        ImVec2 contentPos = ImVec2(nodeScreenPos.x, nodeScreenPos.y + headerHeight);
        ImVec2 contentSize = ImVec2(nodeSize.x, nodeSize.y - headerHeight);
        renderColorWheelContent(drawList, colorWheelNode, contentPos, contentSize);
    } else if (imageNode) {
        ImVec2 contentPos = ImVec2(nodeScreenPos.x, nodeScreenPos.y + headerHeight);
        ImVec2 contentSize = ImVec2(nodeSize.x, nodeSize.y - headerHeight);
        renderImageNodeContent(drawList, imageNode, contentPos, contentSize);
    }
}

void NodeGraph::renderSingleNodePins(ImDrawList* drawList, Node* node, const ImVec2& canvasPos) {
    if (ScriptNode* scriptNode = dynamic_cast<ScriptNode*>(node)) {
        renderScriptNodePins(drawList, scriptNode, canvasPos);
        return;
    }
    float scaledGridSize = m_gridSize * m_viewZoom;
    float headerHeight = m_gridSize * m_viewZoom;
    float pinRadius = 6.0f * m_viewZoom;
    float pinInset = pinRadius + 2.0f * m_viewZoom;
    float pinSpacing = m_gridSize * m_viewZoom;
    float textPadding = 4.0f * m_viewZoom;
    
    ImFont* regularFont = ImGui::GetIO().Fonts->Fonts[0];
    ImFont* boldFont = ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : regularFont;
    float baseRegularSize = regularFont->LegacySize;
    
    ImVec2 nodePixelPos = ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize);
    ImVec2 nodeScreenPos = graphToScreen(nodePixelPos, canvasPos);
    ImVec2 nodeSize = ImVec2(node->size.x * scaledGridSize, node->size.y * scaledGridSize);
    ImVec2 nodeEndPos = ImVec2(nodeScreenPos.x + nodeSize.x, nodeScreenPos.y + nodeSize.y);
    
    float pinStartY = nodeScreenPos.y + headerHeight + pinSpacing * 0.5f;
    
    // Calculate label scale for this node
    float pinLabelMaxWidth = (nodeSize.x * 0.5f) - pinInset - pinRadius - textPadding * 2;
    float pinLabelMaxHeight = pinSpacing * 0.8f;
    float labelScale = 2.0f;
    
    for (size_t i = 0; i < node->inputs.size(); i++) {
        ImVec2 baseLabelSize = ImGui::CalcTextSize(node->inputs[i].name.c_str());
        float scaleWidth = pinLabelMaxWidth / baseLabelSize.x;
        float scaleHeight = pinLabelMaxHeight / baseLabelSize.y;
        labelScale = std::min(labelScale, std::min(scaleWidth, scaleHeight));
    }
    
    for (size_t i = 0; i < node->outputs.size(); i++) {
        ImVec2 baseLabelSize = ImGui::CalcTextSize(node->outputs[i].name.c_str());
        float scaleWidth = pinLabelMaxWidth / baseLabelSize.x;
        float scaleHeight = pinLabelMaxHeight / baseLabelSize.y;
        labelScale = std::min(labelScale, std::min(scaleWidth, scaleHeight));
    }
    
    labelScale = std::max(0.1f, std::min(2.0f, labelScale));
    
    // Apply zoom to label scale, but cap the maximum font size for consistency
    // The final scale used for both positioning AND font size must be the same
    float maxFontSize = baseRegularSize * 1.0f;  // Cap at 1x base font size
    float zoomedLabelFontSize = baseRegularSize * labelScale * m_viewZoom;
    float labelFontSize = std::min(zoomedLabelFontSize, maxFontSize * m_viewZoom);
    // Calculate the effective scale that matches the font size we're actually using
    float effectiveLabelScale = labelFontSize / baseRegularSize;
    
    // Render input pins
    for (size_t i = 0; i < node->inputs.size(); i++) {
        NodePin& pin = node->inputs[i];
        
        float pinY = pinStartY + i * pinSpacing;
        ImVec2 pinPos = ImVec2(nodeScreenPos.x + pinInset, pinY);
        
        pin.position = ImVec2(0.5f, 1.5f + i);
        
        bool isHovered = (m_hoveredPinNodeId == node->id && m_hoveredPinId == pin.id);
        bool isConnectable = isPinConnectable(node, pin);
        bool isGreyedOut = m_isDraggingPin && !isConnectable;
        
        // Draw hover glow effect
        if (isHovered && !isGreyedOut) {
            ImU32 glowColor = IM_COL32(255, 180, 100, 80);
            drawList->AddCircleFilled(pinPos, pinRadius * 2.5f, glowColor);
            drawList->AddCircleFilled(pinPos, pinRadius * 2.0f, IM_COL32(255, 180, 100, 50));
        }
        
        // Draw pin circle
        ImU32 pinColor = isGreyedOut ? IM_COL32(80, 80, 80, 255) : getPinColor(pin.type);
        drawList->AddCircleFilled(pinPos, pinRadius * 1.3f, IM_COL32(0, 0, 0, 60));
        drawList->AddCircleFilled(pinPos, pinRadius, pinColor);
        drawList->AddCircle(pinPos, pinRadius, IM_COL32(20, 20, 20, 255), 0, 1.5f * m_viewZoom);
        
        // Draw pin label
        if (labelFontSize > 1.0f) {
            ImVec2 baseLabelSize = ImGui::CalcTextSize(pin.name.c_str());
            ImVec2 labelPos = ImVec2(pinPos.x + pinRadius + textPadding, 
                                     pinY - (baseLabelSize.y * effectiveLabelScale) * 0.5f);
            
            // Check if this input pin has a connection
            bool hasConnection = false;
            for (const auto& conn : m_connections) {
                if (conn.inputNodeId == node->id && conn.inputPinId == pin.id) {
                    hasConnection = true;
                    break;
                }
            }
            
            // Connected pins get bold font and brighter color
            ImU32 labelColor;
            ImFont* labelFont;
            if (isGreyedOut) {
                labelColor = IM_COL32(80, 80, 80, 255);
                labelFont = regularFont;
            } else if (hasConnection) {
                labelColor = IM_COL32(255, 255, 255, 255);  // Bright white for connected
                labelFont = boldFont;
            } else {
                labelColor = IM_COL32(150, 150, 150, 255);  // Default gray
                labelFont = regularFont;
            }
            
            drawList->AddText(labelFont, labelFontSize, labelPos, labelColor, pin.name.c_str());
        }
    }
    
    // Render output pins
    for (size_t i = 0; i < node->outputs.size(); i++) {
        NodePin& pin = node->outputs[i];
        
        float pinY = pinStartY + i * pinSpacing;
        pin.position = ImVec2(node->size.x - 0.5f, 1.5f + i);
        
        ImVec2 pinPos = ImVec2(nodeEndPos.x - pinInset, pinY);
        
        bool isHovered = (m_hoveredPinNodeId == node->id && m_hoveredPinId == pin.id);
        bool isConnectable = isPinConnectable(node, pin);
        bool isGreyedOut = m_isDraggingPin && !isConnectable;
        
        // Draw hover glow effect
        if (isHovered && !isGreyedOut) {
            ImU32 glowColor = IM_COL32(255, 180, 100, 80);
            drawList->AddCircleFilled(pinPos, pinRadius * 2.5f, glowColor);
            drawList->AddCircleFilled(pinPos, pinRadius * 2.0f, IM_COL32(255, 180, 100, 50));
        }
        
        // Draw pin circle
        ImU32 pinColor = isGreyedOut ? IM_COL32(80, 80, 80, 255) : getPinColor(pin.type);
        drawList->AddCircleFilled(pinPos, pinRadius * 1.3f, IM_COL32(0, 0, 0, 60));
        drawList->AddCircleFilled(pinPos, pinRadius, pinColor);
        drawList->AddCircle(pinPos, pinRadius, IM_COL32(20, 20, 20, 255), 0, 1.5f * m_viewZoom);
        
        // Draw pin label (right-aligned)
        if (labelFontSize > 1.0f) {
            ImVec2 baseLabelSize = ImGui::CalcTextSize(pin.name.c_str());
            float scaledLabelWidth = baseLabelSize.x * effectiveLabelScale;
            ImVec2 labelPos = ImVec2(pinPos.x - pinRadius - textPadding - scaledLabelWidth, 
                                     pinY - (baseLabelSize.y * effectiveLabelScale) * 0.5f);
            ImU32 labelColor = isGreyedOut ? IM_COL32(100, 100, 100, 255) : IM_COL32(200, 200, 200, 255);
            drawList->AddText(regularFont, labelFontSize, labelPos, labelColor, pin.name.c_str());
        }
    }
}

void NodeGraph::renderGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize) {
    // Calculate grid spacing based on zoom
    float gridStep = m_gridSize * m_viewZoom;
    float majorGridStep = gridStep * 4.0f;  // Major lines every 4 cells
    
    // Minimum grid size before we stop drawing minor lines
    const float minGridStep = 8.0f;
    
    // Calculate grid offset based on view pan
    float offsetX = fmodf(m_viewOffset.x * m_viewZoom, majorGridStep);
    float offsetY = fmodf(m_viewOffset.y * m_viewZoom, majorGridStep);
    
    // Make offset positive
    if (offsetX < 0) offsetX += majorGridStep;
    if (offsetY < 0) offsetY += majorGridStep;
    
    // Draw minor grid lines (only if zoomed in enough)
    if (gridStep >= minGridStep) {
        for (float x = offsetX - majorGridStep; x < canvasSize.x + majorGridStep; x += gridStep) {
            drawList->AddLine(
                ImVec2(canvasPos.x + x, canvasPos.y),
                ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y),
                m_gridColorMinor, 1.0f);
        }
        
        for (float y = offsetY - majorGridStep; y < canvasSize.y + majorGridStep; y += gridStep) {
            drawList->AddLine(
                ImVec2(canvasPos.x, canvasPos.y + y),
                ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y),
                m_gridColorMinor, 1.0f);
        }
    }
    
    // Draw major grid lines
    for (float x = offsetX - majorGridStep; x < canvasSize.x + majorGridStep; x += majorGridStep) {
        drawList->AddLine(
            ImVec2(canvasPos.x + x, canvasPos.y),
            ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y),
            m_gridColorMajor, 1.0f);
    }
    
    for (float y = offsetY - majorGridStep; y < canvasSize.y + majorGridStep; y += majorGridStep) {
        drawList->AddLine(
            ImVec2(canvasPos.x, canvasPos.y + y),
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y),
            m_gridColorMajor, 1.0f);
    }
}

void NodeGraph::renderNodeBodies(ImDrawList* drawList, const ImVec2& canvasPos) {
    float scaledGridSize = m_gridSize * m_viewZoom;
    float headerHeight = m_gridSize * m_viewZoom;  // 1 grid cell for header
    
    // Get bold font for titles (index 1 in font array)
    ImFont* boldFont = ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;
    ImFont* regularFont = ImGui::GetIO().Fonts->Fonts[0];
    
    // Base font sizes
    float baseBoldSize = boldFont ? boldFont->LegacySize : regularFont->LegacySize;
    
    // Get nodes sorted by z-order for correct rendering order
    std::vector<Node*> sortedNodes = getNodesSortedByZOrder();
    
    for (auto* node : sortedNodes) {
        // Check if this is a ColorWheel node (uses fixed size, not dynamic)
        ColorWheelNode* colorWheelNode = dynamic_cast<ColorWheelNode*>(node);
        ImageNode* imageNode = dynamic_cast<ImageNode*>(node);
        
        // Dynamically calculate node height based on pin count (only for non-special nodes)
        if (!colorWheelNode && !imageNode) {
            size_t maxPins = std::max(node->inputs.size(), node->outputs.size());
            float dynamicHeight = 1.5f + maxPins;  // 1.5 grid units for header + 1 per pin
            node->size.y = dynamicHeight;
        }
        
        // Convert grid position to screen position
        ImVec2 nodePixelPos = ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize);
        ImVec2 nodeScreenPos = graphToScreen(nodePixelPos, canvasPos);
        
        // Node size in screen pixels (size is in grid units)
        ImVec2 nodeSize = ImVec2(node->size.x * scaledGridSize, node->size.y * scaledGridSize);
        
        ImVec2 nodeEndPos = ImVec2(nodeScreenPos.x + nodeSize.x, nodeScreenPos.y + nodeSize.y);
        
        // Node body background - slightly darker than graph background
        ImU32 bodyColor = IM_COL32(40, 40, 45, 245);
        drawList->AddRectFilled(nodeScreenPos, nodeEndPos, bodyColor, 6.0f * m_viewZoom);
        
        // Node header with subtle gradient (left to right: vibrant to paler)
        ImVec2 headerEndPos = ImVec2(nodeEndPos.x, nodeScreenPos.y + headerHeight);
        
        // Extract RGB components from header color and create gradient versions
        ImU32 hdrColor = node->headerColor;
        int hdrR = (hdrColor >> 0) & 0xFF;
        int hdrG = (hdrColor >> 8) & 0xFF;
        int hdrB = (hdrColor >> 16) & 0xFF;
        int hdrA = (hdrColor >> 24) & 0xFF;
        
        // Left side: slightly more saturated/vibrant
        int leftR = std::min(255, (int)(hdrR * 1.15f));
        int leftG = std::min(255, (int)(hdrG * 0.9f));
        int leftB = std::min(255, (int)(hdrB * 0.9f));
        ImU32 leftColor = IM_COL32(leftR, leftG, leftB, hdrA);
        
        // Right side: paler/lighter version
        int rightR = std::min(255, hdrR + (255 - hdrR) / 3);
        int rightG = std::min(255, hdrG + (255 - hdrG) / 4);
        int rightB = std::min(255, hdrB + (255 - hdrB) / 4);
        ImU32 rightColor = IM_COL32(rightR, rightG, rightB, hdrA);
        
        // Draw gradient header with rounded top corners
        drawList->AddRectFilledMultiColor(
            ImVec2(nodeScreenPos.x, nodeScreenPos.y + 3.0f * m_viewZoom),
            headerEndPos,
            leftColor, rightColor, rightColor, leftColor);
        
        // Draw rounded top corners manually
        drawList->AddRectFilled(nodeScreenPos, headerEndPos, leftColor, 6.0f * m_viewZoom, ImDrawFlags_RoundCornersTop);
        drawList->AddRectFilledMultiColor(
            ImVec2(nodeScreenPos.x + 6.0f * m_viewZoom, nodeScreenPos.y),
            ImVec2(nodeEndPos.x - 6.0f * m_viewZoom, headerEndPos.y),
            leftColor, rightColor, rightColor, leftColor);
        
        // Node border
        ImU32 borderColor = node->selected ? IM_COL32(255, 200, 100, 255) : IM_COL32(70, 70, 80, 255);
        float borderThickness = node->selected ? 2.0f * m_viewZoom : 1.0f * m_viewZoom;
        drawList->AddRect(nodeScreenPos, nodeEndPos, borderColor, 6.0f * m_viewZoom, 0, borderThickness);
        
        // === TITLE TEXT ===
        float headerPadding = 4.0f * m_viewZoom;
        float maxTitleWidth = nodeSize.x - headerPadding * 2;
        float maxTitleHeight = headerHeight - headerPadding * 2;
        
        ImVec2 baseTitleSize = ImGui::CalcTextSize(node->name.c_str());
        
        float scaleToFitWidth = maxTitleWidth / baseTitleSize.x;
        float scaleToFitHeight = maxTitleHeight / baseTitleSize.y;
        
        float titleScale = std::min(scaleToFitWidth, scaleToFitHeight);
        titleScale = std::max(0.1f, std::min(2.0f, titleScale));
        
        ImVec2 titleSize = ImVec2(baseTitleSize.x * titleScale, baseTitleSize.y * titleScale);
        
        ImVec2 titlePos = ImVec2(
            nodeScreenPos.x + (nodeSize.x - titleSize.x) * 0.5f,
            nodeScreenPos.y + (headerHeight - titleSize.y) * 0.5f
        );
        
        ImFont* titleFont = boldFont ? boldFont : regularFont;
        float titleFontSize = baseBoldSize * titleScale;
        if (titleFontSize > 1.0f) {
            drawList->AddText(titleFont, titleFontSize, titlePos, IM_COL32(255, 255, 255, 255), node->name.c_str());
        }
        
        // === CUSTOM NODE CONTENT ===
        if (colorWheelNode) {
            ImVec2 contentPos = ImVec2(nodeScreenPos.x, nodeScreenPos.y + headerHeight);
            ImVec2 contentSize = ImVec2(nodeSize.x, nodeSize.y - headerHeight);
            renderColorWheelContent(drawList, colorWheelNode, contentPos, contentSize);
        } else if (imageNode) {
            ImVec2 contentPos = ImVec2(nodeScreenPos.x, nodeScreenPos.y + headerHeight);
            ImVec2 contentSize = ImVec2(nodeSize.x, nodeSize.y - headerHeight);
            renderImageNodeContent(drawList, imageNode, contentPos, contentSize);
        }
    }
}

void NodeGraph::renderNodePins(ImDrawList* drawList, const ImVec2& canvasPos) {
    float scaledGridSize = m_gridSize * m_viewZoom;
    float pinRadius = 6.0f * m_viewZoom;
    float headerHeight = m_gridSize * m_viewZoom;
    float pinSpacing = m_gridSize * m_viewZoom;
    float textPadding = 8.0f * m_viewZoom;
    float pinInset = 0.5f * scaledGridSize;
    
    ImFont* regularFont = ImGui::GetIO().Fonts->Fonts[0];
    float baseRegularSize = regularFont->LegacySize;
    
    // Calculate a GLOBAL uniform label scale across ALL nodes for consistency
    // This ensures pin labels look the same size on all nodes
    float globalUniformLabelScale = 2.0f;
    
    // Get nodes sorted by z-order for correct rendering order
    std::vector<Node*> sortedNodes = getNodesSortedByZOrder();
    
    for (auto* node : sortedNodes) {
        ImVec2 nodeSize = ImVec2(node->size.x * scaledGridSize, node->size.y * scaledGridSize);
        float pinLabelMaxWidth = (nodeSize.x * 0.5f) - pinInset - pinRadius - textPadding * 2;
        float pinLabelMaxHeight = pinSpacing * 0.8f;
        
        for (size_t i = 0; i < node->inputs.size(); i++) {
            ImVec2 baseLabelSize = ImGui::CalcTextSize(node->inputs[i].name.c_str());
            float scaleWidth = pinLabelMaxWidth / baseLabelSize.x;
            float scaleHeight = pinLabelMaxHeight / baseLabelSize.y;
            float thisScale = std::min(scaleWidth, scaleHeight);
            globalUniformLabelScale = std::min(globalUniformLabelScale, thisScale);
        }
        
        for (size_t i = 0; i < node->outputs.size(); i++) {
            ImVec2 baseLabelSize = ImGui::CalcTextSize(node->outputs[i].name.c_str());
            float scaleWidth = pinLabelMaxWidth / baseLabelSize.x;
            float scaleHeight = pinLabelMaxHeight / baseLabelSize.y;
            float thisScale = std::min(scaleWidth, scaleHeight);
            globalUniformLabelScale = std::min(globalUniformLabelScale, thisScale);
        }
    }
    
    globalUniformLabelScale = std::max(0.1f, std::min(2.0f, globalUniformLabelScale));
    float labelFontSize = baseRegularSize * globalUniformLabelScale;
    
    for (auto* node : sortedNodes) {
        ImVec2 nodePixelPos = ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize);
        ImVec2 nodeScreenPos = graphToScreen(nodePixelPos, canvasPos);
        ImVec2 nodeSize = ImVec2(node->size.x * scaledGridSize, node->size.y * scaledGridSize);
        ImVec2 nodeEndPos = ImVec2(nodeScreenPos.x + nodeSize.x, nodeScreenPos.y + nodeSize.y);
        
        float pinStartY = nodeScreenPos.y + headerHeight + pinSpacing * 0.5f;
        
        // Render input pins
        for (size_t i = 0; i < node->inputs.size(); i++) {
            NodePin& pin = node->inputs[i];
            
            float pinY = pinStartY + i * pinSpacing;
            ImVec2 pinPos = ImVec2(nodeScreenPos.x + pinInset, pinY);
            
            pin.position = ImVec2(0.5f, 1.5f + i);
            
            bool isHovered = (m_hoveredPinNodeId == node->id && m_hoveredPinId == pin.id);
            bool isConnectable = isPinConnectable(node, pin);
            bool isGreyedOut = m_isDraggingPin && !isConnectable;
            
            // Draw hover glow effect (soft orange glow) - only if connectable
            if (isHovered && !isGreyedOut) {
                ImU32 glowColor = IM_COL32(255, 180, 100, 80);
                drawList->AddCircleFilled(pinPos, pinRadius * 2.5f, glowColor);
                drawList->AddCircleFilled(pinPos, pinRadius * 2.0f, IM_COL32(255, 180, 100, 50));
            }
            
            // Draw pin circle
            ImU32 pinColor = isGreyedOut ? IM_COL32(80, 80, 80, 255) : getPinColor(pin.type);
            drawList->AddCircleFilled(pinPos, pinRadius * 1.3f, IM_COL32(0, 0, 0, 60));
            drawList->AddCircleFilled(pinPos, pinRadius, pinColor);
            drawList->AddCircle(pinPos, pinRadius, IM_COL32(20, 20, 20, 255), 0, 1.5f * m_viewZoom);
            
            // Draw pin label
            if (labelFontSize > 1.0f) {
                ImVec2 baseLabelSize = ImGui::CalcTextSize(pin.name.c_str());
                ImVec2 labelPos = ImVec2(pinPos.x + pinRadius + textPadding, 
                                         pinY - (baseLabelSize.y * globalUniformLabelScale) * 0.5f);
                ImU32 labelColor = isGreyedOut ? IM_COL32(100, 100, 100, 255) : IM_COL32(200, 200, 200, 255);
                drawList->AddText(regularFont, labelFontSize, labelPos, labelColor, pin.name.c_str());
            }
        }
        
        // Render output pins
        for (size_t i = 0; i < node->outputs.size(); i++) {
            NodePin& pin = node->outputs[i];
            
            float pinY = pinStartY + i * pinSpacing;
            pin.position = ImVec2(node->size.x - 0.5f, 1.5f + i);
            
            ImVec2 pinPos = ImVec2(nodeEndPos.x - pinInset, pinY);
            
            bool isHovered = (m_hoveredPinNodeId == node->id && m_hoveredPinId == pin.id);
            bool isConnectable = isPinConnectable(node, pin);
            bool isGreyedOut = m_isDraggingPin && !isConnectable;
            
            // Draw hover glow effect (soft orange glow) - only if connectable
            if (isHovered && !isGreyedOut) {
                ImU32 glowColor = IM_COL32(255, 180, 100, 80);
                drawList->AddCircleFilled(pinPos, pinRadius * 2.5f, glowColor);
                drawList->AddCircleFilled(pinPos, pinRadius * 2.0f, IM_COL32(255, 180, 100, 50));
            }
            
            // Draw pin circle
            ImU32 pinColor = isGreyedOut ? IM_COL32(80, 80, 80, 255) : getPinColor(pin.type);
            drawList->AddCircleFilled(pinPos, pinRadius * 1.3f, IM_COL32(0, 0, 0, 60));
            drawList->AddCircleFilled(pinPos, pinRadius, pinColor);
            drawList->AddCircle(pinPos, pinRadius, IM_COL32(20, 20, 20, 255), 0, 1.5f * m_viewZoom);
            
            // Draw pin label (right-aligned)
            if (labelFontSize > 1.0f) {
                ImVec2 baseLabelSize = ImGui::CalcTextSize(pin.name.c_str());
                float scaledLabelWidth = baseLabelSize.x * globalUniformLabelScale;
                ImVec2 labelPos = ImVec2(pinPos.x - pinRadius - textPadding - scaledLabelWidth, 
                                         pinY - (baseLabelSize.y * globalUniformLabelScale) * 0.5f);
                ImU32 labelColor = isGreyedOut ? IM_COL32(100, 100, 100, 255) : IM_COL32(200, 200, 200, 255);
                drawList->AddText(regularFont, labelFontSize, labelPos, labelColor, pin.name.c_str());
            }
        }
    }
}

// Connection rendering moved to src/nodegraph/ConnectionRenderer.cpp

void NodeGraph::handleInput(const ImVec2& canvasPos, const ImVec2& canvasSize, bool canvasHovered, bool canvasActive) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    
    float scaledGridSize = m_gridSize * m_viewZoom;
    // A screen position as a grid position (where a node added from a menu goes)
    auto gridAt = [&](const ImVec2& screen) {
        const ImVec2 g = screenToGraph(screen, canvasPos);
        return ImVec2(std::round(g.x / m_gridSize), std::round(g.y / m_gridSize));
    };
    
    // Reset pin hover state
    m_hoveredPinNodeId = -1;
    m_hoveredPinId = -1;
    
    // Check for pin hover first (higher priority than node hover)
    if (canvasHovered && !m_isPanning && !m_isDraggingNode) {
        PinHitResult pinHit = hitTestPin(mousePos, canvasPos);
        if (pinHit.node) {
            m_hoveredPinNodeId = pinHit.nodeId;
            m_hoveredPinId = pinHit.pinId;
        }
    }
    
    // Check for node hover/selection (only when canvas is hovered and not already interacting)
    m_hoveredNodeId = -1;
    if (canvasHovered && !m_isPanning && !m_isDraggingNode && !m_isDraggingPin && m_hoveredPinNodeId == -1) {
        // Use z-order aware hit detection - find the topmost node at mouse position
        Node* topmostNode = getTopmostNodeAt(mousePos, canvasPos);
        if (topmostNode) {
            m_hoveredNodeId = topmostNode->id;
        }
    }
    
    // Left-click handling
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // First check if we clicked on a pin
        if (m_hoveredPinNodeId != -1) {
            // Start dragging from this pin
            m_isDraggingPin = true;
            m_draggingPinNodeId = m_hoveredPinNodeId;
            m_draggingPinId = m_hoveredPinId;
            
            // Determine if it's an output pin
            Node* node = getNode(m_draggingPinNodeId);
            if (node) {
                m_draggingPinIsOutput = false;
                for (const auto& pin : node->outputs) {
                    if (pin.id == m_draggingPinId) {
                        m_draggingPinIsOutput = true;
                        break;
                    }
                }
            }
        } else if (m_hoveredNodeId != -1) {
            Node* hoveredNode = getNode(m_hoveredNodeId);
            
            // Check if this is a ColorWheel node and if we clicked on an interactive area
            ColorWheelNode* colorNode = dynamic_cast<ColorWheelNode*>(hoveredNode);
            ColorWheelHitArea hitArea = ColorWheelHitArea::None;
            if (colorNode) {
                hitArea = hitTestColorWheel(colorNode, mousePos, canvasPos);
            }
            
            // Ctrl or Shift adds the node to the selection, or takes it out again.
            const bool additive = io.KeyCtrl || io.KeyShift;

            if (hitArea == ColorWheelHitArea::HueRing || hitArea == ColorWheelHitArea::SatValSquare) {
                // Start hue or saturation/value dragging: this node alone is selected
                (hitArea == ColorWheelHitArea::HueRing ? m_isDraggingHue : m_isDraggingSV) = true;
                m_interactingNodeId = m_hoveredNodeId;
                clearSelection();
                m_selectedNodeId = m_hoveredNodeId;
                hoveredNode->selected = true;
                bringNodeToFront(hoveredNode);
                // Immediately handle the interaction to update color on click
                handleColorWheelInteraction(colorNode, mousePos, canvasPos);
            } else if (additive) {
                hoveredNode->selected = !hoveredNode->selected;
                if (hoveredNode->selected) {
                    m_selectedNodeId = hoveredNode->id;
                    bringNodeToFront(hoveredNode);
                } else if (m_selectedNodeId == hoveredNode->id) {
                    const std::vector<int> rest = selectedNodeIds();
                    m_selectedNodeId = rest.empty() ? -1 : rest.back();
                }
            } else {
                // Not on an interactive area - start dragging. Pressing on a node that is already
                // part of a selection drags the whole selection; pressing on any other node selects
                // it alone.
                m_collapseOnRelease = false;
                if (!hoveredNode->selected) {
                    clearSelection();
                    hoveredNode->selected = true;
                } else if (selectedCount() > 1) {
                    m_collapseOnRelease = true;
                }
                m_selectedNodeId = m_hoveredNodeId;
                m_draggingNodeId = m_hoveredNodeId;
                m_isDraggingNode = true;
                m_dragMoved = false;
                m_dragStart.clear();
                for (const auto& n : m_nodes) {
                    if (n->selected) m_dragStart[n->id] = n->position;
                }

                // Calculate drag offset
                ImVec2 nodePixelPos = ImVec2(hoveredNode->position.x * m_gridSize, hoveredNode->position.y * m_gridSize);
                ImVec2 nodeScreenPos = graphToScreen(nodePixelPos, canvasPos);
                m_dragOffset = ImVec2(mousePos.x - nodeScreenPos.x, mousePos.y - nodeScreenPos.y);
                bringNodeToFront(hoveredNode);
            }
            if (m_scriptHooks.onSelected) {
                m_scriptHooks.onSelected(dynamic_cast<ScriptNode*>(getNode(m_selectedNodeId)));
            }
        } else {
            // Clicked on empty space: start a box. Without Ctrl/Shift that also deselects everything.
            m_isBoxSelecting = true;
            m_boxStart = mousePos;
            m_boxEnd = mousePos;
            m_boxBase.clear();
            if (io.KeyCtrl || io.KeyShift) {
                for (int id : selectedNodeIds()) m_boxBase.insert(id);
            } else {
                clearSelection();
                if (m_scriptHooks.onSelected) {
                    m_scriptHooks.onSelected(nullptr);
                }
            }
        }
    }

    // Box select: every node the box touches is selected, as the box is dragged
    if (m_isBoxSelecting) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_boxEnd = mousePos;
            const ImVec2 lo(std::min(m_boxStart.x, m_boxEnd.x), std::min(m_boxStart.y, m_boxEnd.y));
            const ImVec2 hi(std::max(m_boxStart.x, m_boxEnd.x), std::max(m_boxStart.y, m_boxEnd.y));
            const float cell = m_gridSize * m_viewZoom;
            for (auto& n : m_nodes) {
                const ImVec2 at = graphToScreen(ImVec2(n->position.x * m_gridSize, n->position.y * m_gridSize), canvasPos);
                const bool touches = at.x <= hi.x && at.x + n->size.x * cell >= lo.x && at.y <= hi.y && at.y + n->size.y * cell >= lo.y;
                n->selected = touches || m_boxBase.count(n->id) > 0;
            }
            const std::vector<int> picked = selectedNodeIds();
            if (m_selectedNodeId == -1 || !getNode(m_selectedNodeId) || !getNode(m_selectedNodeId)->selected) {
                m_selectedNodeId = picked.empty() ? -1 : picked.back();
            }
        } else {
            m_isBoxSelecting = false;
            if (m_scriptHooks.onSelected) {
                m_scriptHooks.onSelected(dynamic_cast<ScriptNode*>(getNode(m_selectedNodeId)));
            }
        }
    }
    
    // Handle pin dragging release
    if (m_isDraggingPin) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Mouse released - check if we're over a valid target pin
            PinHitResult targetHit = hitTestPin(mousePos, canvasPos);
            if (targetHit.node && targetHit.node->id != m_draggingPinNodeId) {
                Node* sourceNode = getNode(m_draggingPinNodeId);
                NodePin* sourcePin = nullptr;
                
                // Find the source pin
                if (sourceNode) {
                    if (m_draggingPinIsOutput) {
                        for (auto& pin : sourceNode->outputs) {
                            if (pin.id == m_draggingPinId) {
                                sourcePin = &pin;
                                break;
                            }
                        }
                    } else {
                        for (auto& pin : sourceNode->inputs) {
                            if (pin.id == m_draggingPinId) {
                                sourcePin = &pin;
                                break;
                            }
                        }
                    }
                }
                
                // Check if connection is valid
                if (sourceNode && sourcePin && targetHit.pin && 
                    canConnect(sourceNode, *sourcePin, targetHit.node, *targetHit.pin)) {
                    
                    ScriptNode* scriptFrom = dynamic_cast<ScriptNode*>(m_draggingPinIsOutput ? sourceNode : targetHit.node);
                    ScriptNode* scriptTo = dynamic_cast<ScriptNode*>(m_draggingPinIsOutput ? targetHit.node : sourceNode);
                    if (scriptFrom && scriptTo) {
                        // A script graph is rebuilt from its text: report the wish, don't wire it here.
                        int fromPinId = m_draggingPinIsOutput ? m_draggingPinId : targetHit.pinId;
                        const int toPinId = m_draggingPinIsOutput ? targetHit.pinId : m_draggingPinId;
                        if (scriptFrom->carriesVariable()) {
                            if (m_scriptHooks.onConnectGetter) m_scriptHooks.onConnectGetter(*scriptFrom, *scriptTo, toPinId);
                        } else if (scriptFrom->isData()) {
                            // A computed value is plugged into a pin by the node it was made for; one that floats can be wired.
                            if (scriptFrom->floating && m_scriptHooks.onConnectValue) m_scriptHooks.onConnectValue(*scriptFrom, *scriptTo, toPinId);
                        } else if (m_scriptHooks.onConnectExec) {
                            m_scriptHooks.onConnectExec(*scriptFrom, fromPinId, *scriptTo);
                        }
                    } else {
                        // Create the connection (always from output to input)
                        if (m_draggingPinIsOutput) {
                            addConnection(m_draggingPinNodeId, m_draggingPinId,
                                          targetHit.nodeId, targetHit.pinId);
                        } else {
                            addConnection(targetHit.nodeId, targetHit.pinId,
                                          m_draggingPinNodeId, m_draggingPinId);
                        }
                        m_hasUnsavedChanges = true;
                    }
                }
            } else if (!targetHit.node && dynamic_cast<ScriptNode*>(getNode(m_draggingPinNodeId))) {
                // Let go of a wire on empty canvas: offer to add a node there (execution wire), or
                // to compute the value with a node (input pin).
                Node* from = getNode(m_draggingPinNodeId);
                const NodePin* dragged = nullptr;
                for (const auto& p : from->outputs) { if (p.id == m_draggingPinId) dragged = &p; }
                for (const auto& p : from->inputs) { if (p.id == m_draggingPinId) dragged = &p; }
                if (dragged && ImGui::GetMousePos().x >= canvasPos.x && ImGui::GetMousePos().x <= canvasPos.x + canvasSize.x &&
                    ImGui::GetMousePos().y >= canvasPos.y && ImGui::GetMousePos().y <= canvasPos.y + canvasSize.y) {
                    if (dragged->isOutput && dragged->type == PinType::Exec) {
                        m_scriptMenu = {ScriptMenuContext::Kind::ExecDrop, m_draggingPinNodeId, m_draggingPinId, gridAt(mousePos)};
                        m_menuConnections.clear();
                        m_menuWireOnly = false;
                        m_showContextMenu = true;
                        m_contextMenuSpawnPos = screenToGraph(mousePos, canvasPos);
                    } else if (dragged->isOutput && dragged->type != PinType::Exec && dynamic_cast<ScriptNode*>(from)->carriesVariable()) {
                        m_scriptMenu = {ScriptMenuContext::Kind::GetterDrop, m_draggingPinNodeId, m_draggingPinId, gridAt(mousePos)};
                        m_menuConnections.clear();
                        m_menuWireOnly = false;
                        m_showContextMenu = true;
                        m_contextMenuSpawnPos = screenToGraph(mousePos, canvasPos);
                    } else if (!dragged->isOutput && dragged->type != PinType::Exec) {
                        m_scriptMenu = {ScriptMenuContext::Kind::Pin, m_draggingPinNodeId, m_draggingPinId, gridAt(mousePos)};
                        m_menuConnections.clear();
                        m_menuWireOnly = false;
                        m_showContextMenu = true;
                        m_contextMenuSpawnPos = screenToGraph(mousePos, canvasPos);
                    }
                }
            }
            
            // Stop dragging
            m_isDraggingPin = false;
            m_draggingPinNodeId = -1;
            m_draggingPinId = -1;
        }
    }
    
    // Delete key to remove selected node (only if canvas is hovered/focused)
    if (canvasHovered && !io.WantTextInput && !m_isBoxSelecting && selectedCount() > 0 && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        deleteSelected();
    }

    // Ctrl+A selects every node
    if (canvasHovered && !io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
        selectAll();
        if (m_scriptHooks.onSelected) {
            m_scriptHooks.onSelected(dynamic_cast<ScriptNode*>(getNode(m_selectedNodeId)));
        }
    }

    // Ctrl+D duplicates the selected nodes of a script graph (the copies are connected to nothing)
    if (m_hasScriptNodes && canvasHovered && !io.WantTextInput && !m_isBoxSelecting && selectedCount() > 0 && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        duplicateSelected();
    }

    // Ctrl+D to duplicate selected node (material graphs)
    if (!m_hasScriptNodes && canvasHovered && m_selectedNodeId != -1 && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
        duplicateSelectedNode(canvasPos);
    }

    // Ctrl+C to copy selected node
    if (!m_hasScriptNodes && canvasHovered && m_selectedNodeId != -1 && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
        copySelectedNode();
    }

    // Ctrl+V to paste node at cursor position
    if (!m_hasScriptNodes && canvasHovered && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
        pasteNode(canvasPos, mousePos);
    }
    
    // Drag node - continue even if mouse leaves canvas
    if (m_isDraggingNode && m_draggingNodeId != -1) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            Node* node = getNode(m_draggingNodeId);
            if (node) {
                // Calculate new position in graph coordinates
                ImVec2 newScreenPos = ImVec2(mousePos.x - m_dragOffset.x, mousePos.y - m_dragOffset.y);
                ImVec2 newGraphPos = screenToGraph(newScreenPos, canvasPos);
                
                // Convert to grid units and snap
                float newGridX = std::round(newGraphPos.x / m_gridSize);
                float newGridY = std::round(newGraphPos.y / m_gridSize);
                
                // Every selected node moves by the same amount as the one held
                auto held = m_dragStart.find(node->id);
                if (held != m_dragStart.end()) {
                    const float dx = newGridX - held->second.x;
                    const float dy = newGridY - held->second.y;
                    for (const auto& start : m_dragStart) {
                        Node* moving = getNode(start.first);
                        if (!moving) continue;
                        const float x = start.second.x + dx;
                        const float y = start.second.y + dy;
                        if (moving->position.x != x || moving->position.y != y) {
                            moving->position.x = x;
                            moving->position.y = y;
                            m_dragMoved = true;
                            m_hasUnsavedChanges = true;
                        }
                    }
                }
            }
        } else {
            // Let go. If nodes moved, a script graph hears where they ended up (to keep the layout);
            // if the press was a plain click on one node of a selection, that node alone stays selected.
            if (m_dragMoved) {
                // Everything that was dragged lands on the grid (a node in an automatic layout may sit between squares).
                for (const auto& start : m_dragStart) {
                    if (Node* n = getNode(start.first)) n->position = ImVec2(std::round(n->position.x), std::round(n->position.y));
                }
                std::vector<ScriptNode*> moved;
                for (const auto& start : m_dragStart) {
                    ScriptNode* n = dynamic_cast<ScriptNode*>(getNode(start.first));
                    if (n && (n->position.x != start.second.x || n->position.y != start.second.y)) moved.push_back(n);
                }
                if (!moved.empty() && m_scriptHooks.onNodesMoved) m_scriptHooks.onNodesMoved(moved);
            } else if (m_collapseOnRelease) {
                const int keep = m_draggingNodeId;
                clearSelection();
                if (Node* n = getNode(keep)) {
                    n->selected = true;
                    m_selectedNodeId = keep;
                }
                if (m_scriptHooks.onSelected) m_scriptHooks.onSelected(dynamic_cast<ScriptNode*>(getNode(m_selectedNodeId)));
            }
            m_isDraggingNode = false;
            m_draggingNodeId = -1;
            m_dragStart.clear();
            m_dragMoved = false;
            m_collapseOnRelease = false;
        }
    }
    
    // Handle ColorWheel interaction - continue even if mouse leaves the wheel area
    if ((m_isDraggingHue || m_isDraggingSV) && m_interactingNodeId != -1) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ColorWheelNode* colorNode = dynamic_cast<ColorWheelNode*>(getNode(m_interactingNodeId));
            if (colorNode) {
                handleColorWheelInteraction(colorNode, mousePos, canvasPos);
            }
        } else {
            // Mouse released - stop ColorWheel interaction
            m_isDraggingHue = false;
            m_isDraggingSV = false;
            m_interactingNodeId = -1;
        }
    }
    
    // Right-click handling: quick click opens context menu, drag-and-hold pans
    static bool rightClickStartedOnEmpty = false;
    static ImVec2 rightClickStartPos;
    static bool hasDraggedSincRightClick = false;
    static ScriptMenuContext rightClickScript;
    static std::vector<NodeConnection> rightClickWires;  // what "Delete connection" would cut
    static bool rightClickWireOnly = false;              // the click was on a wire, or on a pin of a material node

    // Start tracking right-click
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !m_isDraggingNode && !m_isDraggingPin) {
        rightClickStartedOnEmpty = (m_hoveredNodeId == -1);  // Only context menu on empty space
        rightClickWires.clear();
        rightClickWireOnly = false;
        if (m_hoveredPinNodeId != -1) {
            // A pin with wires on it: right-click offers to cut them.
            for (const auto& c : m_connections) {
                if ((c.inputNodeId == m_hoveredPinNodeId && c.inputPinId == m_hoveredPinId) ||
                    (c.outputNodeId == m_hoveredPinNodeId && c.outputPinId == m_hoveredPinId)) {
                    rightClickWires.push_back(c);
                }
            }
            if (!rightClickWires.empty() && !m_hasScriptNodes) {
                rightClickWireOnly = true;
                rightClickStartedOnEmpty = true;
            }
        } else if (m_hoveredNodeId == -1) {
            // A wire on empty canvas.
            const int wire = hitTestConnection(mousePos, canvasPos);
            if (wire >= 0) {
                rightClickWires.push_back(m_connections[static_cast<size_t>(wire)]);
                rightClickWireOnly = true;
                rightClickStartedOnEmpty = true;
            }
        }
        if (m_hasScriptNodes) {
            // In a script graph a right-click on a node or an input pin has its own menu.
            rightClickScript = ScriptMenuContext();
            if (m_hoveredPinNodeId != -1) {
                Node* hovered = getNode(m_hoveredPinNodeId);
                bool isInput = false;
                if (hovered) {
                    for (const auto& p : hovered->inputs) { if (p.id == m_hoveredPinId && p.type != PinType::Exec) isInput = true; }
                }
                rightClickScript = {isInput ? ScriptMenuContext::Kind::Pin : ScriptMenuContext::Kind::Node, m_hoveredPinNodeId, m_hoveredPinId};
                rightClickStartedOnEmpty = true;
            } else if (m_hoveredNodeId != -1) {
                rightClickScript = {ScriptMenuContext::Kind::Node, m_hoveredNodeId, -1};
                rightClickStartedOnEmpty = true;
            }
        }
        rightClickStartPos = mousePos;
        hasDraggedSincRightClick = false;
        m_isPanning = true;
        m_lastMousePos = mousePos;
        m_panStartOffset = m_viewOffset;
    }
    
    // Continue panning while right mouse is held (even if cursor leaves canvas)
    if (m_isPanning) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            // Use incremental delta for smoother panning
            ImVec2 delta = ImVec2(mousePos.x - m_lastMousePos.x, mousePos.y - m_lastMousePos.y);
            
            // Check if we've dragged enough to consider it a pan (not a click)
            float dragDist = sqrtf((mousePos.x - rightClickStartPos.x) * (mousePos.x - rightClickStartPos.x) +
                                   (mousePos.y - rightClickStartPos.y) * (mousePos.y - rightClickStartPos.y));
            if (dragDist > 3.0f) {
                hasDraggedSincRightClick = true;
            }
            
            m_viewOffset.x += delta.x / m_viewZoom;
            m_viewOffset.y += delta.y / m_viewZoom;
            m_lastMousePos = mousePos;
        } else {
            // Mouse released - stop panning
            m_isPanning = false;
            
            // If we didn't drag and clicked on empty space, show context menu
            if (!hasDraggedSincRightClick && rightClickStartedOnEmpty) {
                m_showContextMenu = true;
                m_contextMenuSpawnPos = screenToGraph(rightClickStartPos, canvasPos);
                m_scriptMenu = m_hasScriptNodes ? rightClickScript : ScriptMenuContext();
                m_scriptMenu.spawn = gridAt(rightClickStartPos);
                m_menuConnections = rightClickWires;
                m_menuWireOnly = rightClickWireOnly;
            }
        }
    }
    
    // Mouse wheel zooming - zoom towards cursor position
    if (canvasHovered && io.MouseWheel != 0.0f && !m_isDraggingNode) {
        // Get the graph position under the mouse BEFORE zooming
        ImVec2 mouseGraphPosBefore = screenToGraph(mousePos, canvasPos);
        
        // Calculate zoom
        float zoomDelta = io.MouseWheel * 0.1f;
        float oldZoom = m_viewZoom;
        m_userZoom = std::clamp(m_userZoom + zoomDelta * m_userZoom, 0.1f, 4.0f);
        m_viewZoom = m_userZoom * PropulsionMKI::UI::getUIScale();
        
        // Adjust offset so the point under the mouse stays stationary
        if (oldZoom != m_viewZoom) {
            // After zoom, the same graph position should be under the mouse
            // screenPos = canvasPos + (graphPos + viewOffset) * viewZoom
            // So: viewOffset = (screenPos - canvasPos) / viewZoom - graphPos
            m_viewOffset.x = (mousePos.x - canvasPos.x) / m_viewZoom - mouseGraphPosBefore.x;
            m_viewOffset.y = (mousePos.y - canvasPos.y) / m_viewZoom - mouseGraphPosBefore.y;
        }
    }
}

ImVec2 NodeGraph::screenToGraph(const ImVec2& screenPos, const ImVec2& canvasPos) const {
    return ImVec2(
        (screenPos.x - canvasPos.x) / m_viewZoom - m_viewOffset.x,
        (screenPos.y - canvasPos.y) / m_viewZoom - m_viewOffset.y
    );
}

ImVec2 NodeGraph::graphToScreen(const ImVec2& graphPos, const ImVec2& canvasPos) const {
    return ImVec2(
        canvasPos.x + (graphPos.x + m_viewOffset.x) * m_viewZoom,
        canvasPos.y + (graphPos.y + m_viewOffset.y) * m_viewZoom
    );
}

void NodeGraph::addNode(std::unique_ptr<Node> node) {
    // Only assign a new ID if the node doesn't already have one (id < 0 means unassigned)
    // When loading from file, nodes already have IDs; when created from context menu, 
    // the ID is set before calling addNode
    if (node->id < 0) {
        node->id = m_nextNodeId++;
    } else {
        // Ensure m_nextNodeId is always higher than any existing node ID
        if (node->id >= m_nextNodeId) {
            m_nextNodeId = node->id + 1;
        }
    }
    m_nodes.push_back(std::move(node));
}

void NodeGraph::removeNode(int nodeId) {
    // Remove all connections to/from this node
    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(),
            [nodeId](const NodeConnection& conn) {
                return conn.inputNodeId == nodeId || conn.outputNodeId == nodeId;
            }),
        m_connections.end());
    
    // Remove the node
    m_nodes.erase(
        std::remove_if(m_nodes.begin(), m_nodes.end(),
            [nodeId](const std::unique_ptr<Node>& node) {
                return node->id == nodeId;
            }),
        m_nodes.end());
}

Node* NodeGraph::getNode(int nodeId) {
    for (auto& node : m_nodes) {
        if (node->id == nodeId) {
            return node.get();
        }
    }
    return nullptr;
}

EvaluatedMaterial NodeGraph::evaluateMaterial() const {
    EvaluatedMaterial result;
    
    // Find the MaterialOutput node
    const MaterialOutputNode* outputNode = nullptr;
    for (const auto& node : m_nodes) {
        outputNode = dynamic_cast<const MaterialOutputNode*>(node.get());
        if (outputNode) break;
    }
    
    if (!outputNode) {
        return result;  // Return defaults if no output node
    }
    
    // For each input pin on the output node, find what's connected to it
    for (const auto& inputPin : outputNode->inputs) {
        // Find connection to this input pin
        const NodeConnection* connection = nullptr;
        for (const auto& conn : m_connections) {
            if (conn.inputNodeId == outputNode->id && conn.inputPinId == inputPin.id) {
                connection = &conn;
                break;
            }
        }
        
        if (!connection) {
            continue;  // No connection to this pin, use default
        }
        
        // Find the source node
        const Node* sourceNode = nullptr;
        for (const auto& node : m_nodes) {
            if (node->id == connection->outputNodeId) {
                sourceNode = node.get();
                break;
            }
        }
        
        if (!sourceNode) {
            continue;
        }
        
        // Get the value from the source node based on its type
        const ColorWheelNode* colorWheel = dynamic_cast<const ColorWheelNode*>(sourceNode);
        const ImageNode* imageNode = dynamic_cast<const ImageNode*>(sourceNode);
        
        // Map input pin IDs to material properties
        // Pin IDs are defined in MaterialOutputNode constructor:
        // 1=Base Color, 2=Metallic, 3=Roughness, 4=Specular, 5=Normal, 14=Height,
        // 6=Emissive Color, 7=Emissive Strength, 8=AO, 9=Opacity, 
        // 10=Opacity Mask, 11=Refraction, 12=Subsurface Color, 13=Subsurface Strength
        
        switch (inputPin.id) {
            case 1:  // Base Color
                if (colorWheel) {
                    result.baseColor = colorWheel->color;
                } else if (imageNode && !imageNode->selectedTexturePath.empty()) {
                    result.albedoTexturePath = imageNode->selectedTexturePath;
                    result.useAlbedoTexture = true;
                }
                break;
            case 2:  // Metallic
                if (imageNode && !imageNode->selectedTexturePath.empty()) {
                    result.metallicTexturePath = imageNode->selectedTexturePath;
                    result.useMetallicTexture = true;
                    result.metallicStrength = imageNode->strength;
                    result.invertMetallic = imageNode->invertTexture;
                }
                break;
            case 3:  // Roughness
                if (imageNode && !imageNode->selectedTexturePath.empty()) {
                    result.roughnessTexturePath = imageNode->selectedTexturePath;
                    result.useRoughnessTexture = true;
                    result.roughnessStrength = imageNode->strength;
                    result.invertRoughness = imageNode->invertTexture;
                }
                break;
            case 4:  // Specular
                break;
            case 5:  // Normal
                if (imageNode && !imageNode->selectedTexturePath.empty()) {
                    result.normalTexturePath = imageNode->selectedTexturePath;
                    result.useNormalTexture = true;
                    result.normalStrength = imageNode->strength;
                    result.invertNormal = imageNode->invertTexture;
                }
                break;
            case 6:  // Emissive Color
                if (colorWheel) {
                    result.emissiveColor = colorWheel->color;
                } else if (imageNode && !imageNode->selectedTexturePath.empty()) {
                    result.emissiveTexturePath = imageNode->selectedTexturePath;
                    result.useEmissiveTexture = true;
                    result.emissiveTextureStrength = imageNode->strength;
                }
                break;
            case 7:  // Emissive Strength
                break;
            case 8:  // Ambient Occlusion
                if (imageNode && !imageNode->selectedTexturePath.empty()) {
                    result.aoTexturePath = imageNode->selectedTexturePath;
                    result.useAOTexture = true;
                    result.aoStrength = imageNode->strength;
                    result.invertAO = imageNode->invertTexture;
                }
                break;
            case 9:  // Opacity
            case 10: // Opacity Mask
                if (imageNode && !imageNode->selectedTexturePath.empty()) {
                    result.opacityTexturePath = imageNode->selectedTexturePath;
                    result.useOpacityTexture = true;
                }
                break;
            case 11: // Refraction
                break;
            case 12: // Subsurface Color
                if (colorWheel) {
                    result.subsurfaceColor = colorWheel->color;
                }
                break;
            case 13: // Subsurface Strength
                break;
            case 14: // Height
                if (imageNode && !imageNode->selectedTexturePath.empty()) {
                    result.heightTexturePath = imageNode->selectedTexturePath;
                    result.useHeightTexture = true;
                    result.heightStrength = imageNode->strength;
                    result.invertHeight = imageNode->invertTexture;
                }
                break;
        }
    }
    
    return result;
}

void NodeGraph::addConnection(int outNodeId, int outPinId, int inNodeId, int inPinId) {
    // Remove any existing connection to the input pin
    removeConnection(inNodeId, inPinId);
    
    NodeConnection conn;
    conn.outputNodeId = outNodeId;
    conn.outputPinId = outPinId;
    conn.inputNodeId = inNodeId;
    conn.inputPinId = inPinId;
    m_connections.push_back(conn);
}

void NodeGraph::removeConnection(int inNodeId, int inPinId) {
    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(),
            [inNodeId, inPinId](const NodeConnection& conn) {
                return conn.inputNodeId == inNodeId && conn.inputPinId == inPinId;
            }),
        m_connections.end());
}

void NodeGraph::resetView() {
    m_viewOffset = ImVec2(0.0f, 0.0f);
    m_userZoom = 1.0f;
    m_viewZoom = m_userZoom * PropulsionMKI::UI::getUIScale();
}

ImVec2 NodeGraph::snapToGrid(const ImVec2& pos) const {
    return ImVec2(
        std::round(pos.x),
        std::round(pos.y)
    );
}

namespace {

std::string lowered(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

/// How well one typed word fits an entry: 0 starts its name, 1 starts a word of it, 2 is in the name,
/// 3 is only in its category or keywords; -1 is nowhere.
int wordRank(const std::string& name, const std::string& other, const std::string& word) {
    const size_t at = name.find(word);
    if (at == 0) return 0;
    if (at != std::string::npos) return name[at - 1] == ' ' ? 1 : 2;
    return other.find(word) != std::string::npos ? 3 : -1;
}

}  // namespace

bool NodeGraph::drawMenuSearch() {
    if (!m_scriptHooks.searchResults) return false;
    ImGuiIO& io = ImGui::GetIO();
    // Nothing shows until a key is pressed: the first letter opens the field, and goes into it.
    const bool typed = !io.InputQueueCharacters.empty();
    if (m_menuSearch[0] == '\0' && !typed) {
        m_searchShown = false;
        return false;
    }
    if (!m_searchLoaded) {
        m_searchCandidates = m_scriptHooks.searchResults(m_scriptMenu);
        m_searchLoaded = true;
    }
    if (m_searchCandidates.empty()) {
        m_searchShown = false;
        return false;  // this menu adds nothing, so there is nothing to find
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        return true;
    }

    const float width = PropulsionMKI::UI::scaled(340.0f);
    if (!m_searchShown) {
        // The field only takes the keyboard a frame after it is first drawn, which is too late for the key that
        // opened it: those characters are put in by hand, and the field then starts with the cursor after them.
        size_t length = strlen(m_menuSearch);
        for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
            const ImWchar c = io.InputQueueCharacters[i];
            char utf8[5] = {};
            if (c < 32 || c == 127) continue;
            if (c < 0x80) {
                utf8[0] = static_cast<char>(c);
            } else if (c < 0x800) {
                utf8[0] = static_cast<char>(0xC0 | (c >> 6));
                utf8[1] = static_cast<char>(0x80 | (c & 0x3F));
            } else if (c < 0x10000) {
                utf8[0] = static_cast<char>(0xE0 | (c >> 12));
                utf8[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                utf8[2] = static_cast<char>(0x80 | (c & 0x3F));
            } else {
                utf8[0] = static_cast<char>(0xF0 | (c >> 18));
                utf8[1] = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
                utf8[2] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                utf8[3] = static_cast<char>(0x80 | (c & 0x3F));
            }
            const size_t n = strlen(utf8);
            if (length + n >= sizeof(m_menuSearch)) break;
            memcpy(m_menuSearch + length, utf8, n + 1);
            length += n;
        }
        io.InputQueueCharacters.resize(0);
        m_searchSelected = 0;
        m_searchCursorToEnd = true;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(width);
    auto keepCursorAtEnd = [](ImGuiInputTextCallbackData* data) -> int {
        bool* pending = static_cast<bool*>(data->UserData);
        if (*pending) {
            // Focusing a field selects all of it, and the next letter would replace what was typed.
            data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
            *pending = false;
        }
        return 0;
    };
    if (ImGui::InputTextWithHint("##menusearch", "Search nodes...", m_menuSearch, sizeof(m_menuSearch),
                                 ImGuiInputTextFlags_CallbackAlways, keepCursorAtEnd, &m_searchCursorToEnd)) {
        m_searchSelected = 0;
    }
    m_searchShown = true;

    // The entries that have every word typed, best fit first (and in the menu's own order among equals).
    std::vector<std::string> words;
    {
        const std::string query = lowered(m_menuSearch);
        size_t start = 0;
        while (start < query.size()) {
            const size_t end = query.find(' ', start);
            if (end == std::string::npos) {
                words.push_back(query.substr(start));
                break;
            }
            if (end > start) words.push_back(query.substr(start, end - start));
            start = end + 1;
        }
    }
    struct Hit {
        int rank;
        size_t index;
    };
    std::vector<Hit> hits;
    for (size_t i = 0; i < m_searchCandidates.size(); i++) {
        const ScriptSearchResult& r = m_searchCandidates[i];
        const std::string name = lowered(r.label), other = lowered(r.category + " " + r.keywords);
        int rank = 0;
        for (const std::string& word : words) {
            const int fit = wordRank(name, other, word);
            if (fit < 0) {
                rank = -1;
                break;
            }
            rank += fit;
        }
        if (rank >= 0) hits.push_back({rank, i});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.rank < b.rank; });

    bool moved = false;
    if (!hits.empty()) {
        const int last = static_cast<int>(hits.size()) - 1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { m_searchSelected++; moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { m_searchSelected--; moved = true; }
        m_searchSelected = (std::max)(0, (std::min)(m_searchSelected, last));
    }

    int picked = -1;
    if (hits.empty()) {
        ImGui::TextDisabled("Nothing matches");
    } else {
        const float row = ImGui::GetTextLineHeightWithSpacing();
        const float height = (std::min)(static_cast<int>(hits.size()), 10) * row + ImGui::GetStyle().WindowPadding.y;
        ImGui::BeginChild("##menusearchresults", ImVec2(width, height), false);
        for (size_t k = 0; k < hits.size(); k++) {
            const ScriptSearchResult& r = m_searchCandidates[hits[k].index];
            const bool current = static_cast<int>(k) == m_searchSelected;
            ImGui::PushID(static_cast<int>(hits[k].index));
            if (ImGui::Selectable(r.label.c_str(), current)) picked = static_cast<int>(k);
            if (ImGui::IsItemHovered() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) m_searchSelected = static_cast<int>(k);
            if (!r.category.empty()) {
                const ImVec2 size = ImGui::CalcTextSize(r.category.c_str());
                ImGui::GetWindowDrawList()->AddText(ImVec2(ImGui::GetItemRectMax().x - size.x - ImGui::GetStyle().ItemSpacing.x, ImGui::GetItemRectMin().y),
                                                    ImGui::GetColorU32(ImGuiCol_TextDisabled), r.category.c_str());
            }
            if (current && moved) ImGui::SetScrollHereY();
            if (ImGui::IsItemHovered() && !r.tooltip.empty()) PropulsionMKI::UI::tooltip("%s", r.tooltip.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (picked < 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) picked = m_searchSelected;
    }

    if (picked >= 0) {
        const ScriptSearchResult& r = m_searchCandidates[hits[static_cast<size_t>(picked)].index];
        if (r.pick) r.pick();
        ImGui::CloseCurrentPopup();
    }
    return true;
}

void NodeGraph::renderContextMenu(const ImVec2& canvasPos) {
    if (m_showContextMenu) {
        ImGui::OpenPopup("NodeGraphContextMenu");
        m_showContextMenu = false;  // Only open once
        m_menuSearch[0] = '\0';
        m_searchShown = false;
        m_searchLoaded = false;
        m_searchSelected = 0;
        m_searchCandidates.clear();
    }

    // The canvas has no window padding of its own, and the popup (and its submenus) would inherit that.
    PropulsionMKI::UI::pushPopupPadding();

    if (m_hasScriptNodes) {
        // A script graph: the host draws the menu for where the user clicked, unless the user is typing to find a node.
        if (ImGui::BeginPopup("NodeGraphContextMenu")) {
            if (!m_menuWireOnly && drawMenuSearch()) {
                // the results are the menu
            } else {
                if (!m_menuWireOnly && m_scriptHooks.drawMenu) m_scriptHooks.drawMenu(m_scriptMenu);
                drawConnectionMenuItems(!m_menuWireOnly);
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        return;
    }

    if (ImGui::BeginPopup("NodeGraphContextMenu")) {
        if (m_menuWireOnly) {
            drawConnectionMenuItems(false);
            ImGui::EndPopup();
            ImGui::PopStyleVar();
            return;
        }
        ImGui::Text("Add Node");
        ImGui::Separator();
        
        if (ImGui::MenuItem("Color Wheel")) {
            // Calculate spawn position: center the node at the cursor position
            // Node is 8 wide x 7 tall in grid units, so offset by half
            float spawnGridX = std::round(m_contextMenuSpawnPos.x / m_gridSize) - 4.0f;  // Half of width
            float spawnGridY = std::round(m_contextMenuSpawnPos.y / m_gridSize) - 3.5f;  // Half of height
            
            auto colorNode = std::make_unique<ColorWheelNode>(m_nextNodeId++, ImVec2(spawnGridX, spawnGridY));
            addNode(std::move(colorNode));
            m_hasUnsavedChanges = true;
        }
        
        if (ImGui::MenuItem("Image")) {
            // Calculate spawn position: center the node at the cursor position
            // Node is 6 wide x 8 tall in grid units, so offset by half
            float spawnGridX = std::round(m_contextMenuSpawnPos.x / m_gridSize) - 3.0f;  // Half of width
            float spawnGridY = std::round(m_contextMenuSpawnPos.y / m_gridSize) - 4.0f;  // Half of height
            
            auto imageNode = std::make_unique<ImageNode>(m_nextNodeId++, ImVec2(spawnGridX, spawnGridY));
            addNode(std::move(imageNode));
            m_hasUnsavedChanges = true;
        }
        
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

void NodeGraph::deleteConnection(const NodeConnection& conn) {
    ScriptNode* from = dynamic_cast<ScriptNode*>(getNode(conn.outputNodeId));
    ScriptNode* to = dynamic_cast<ScriptNode*>(getNode(conn.inputNodeId));
    if (from && to) {
        // A script graph is rebuilt from its text: the host decides what cutting this wire does to it.
        if (m_scriptHooks.onDeleteConnection) m_scriptHooks.onDeleteConnection(*from, *to, conn.inputPinId);
        return;
    }
    removeConnection(conn.inputNodeId, conn.inputPinId);
    m_hasUnsavedChanges = true;
}

void NodeGraph::drawConnectionMenuItems(bool separator) {
    if (m_menuConnections.empty()) return;
    if (separator) ImGui::Separator();

    // In a script graph an execution wire can't be cut: a statement has to stay in its chain.
    bool execution = false;
    for (const NodeConnection& c : m_menuConnections) {
        Node* from = getNode(c.outputNodeId);
        if (!from || !dynamic_cast<ScriptNode*>(from)) continue;
        for (const auto& pin : from->outputs) {
            if (pin.id == c.outputPinId && pin.type == PinType::Exec) execution = true;
        }
    }
    const std::string label = m_menuConnections.size() == 1 ? "Delete connection" : "Delete " + std::to_string(m_menuConnections.size()) + " connections";
    if (execution) {
        ImGui::BeginDisabled();
        ImGui::MenuItem(label.c_str());
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            PropulsionMKI::UI::tooltip("A statement has to stay in its chain, so this wire can't be cut.\nMove the node (drag its input wire) or delete it instead.");
        }
        return;
    }
    if (ImGui::MenuItem(label.c_str())) {
        const std::vector<NodeConnection> doomed = m_menuConnections;
        for (const NodeConnection& c : doomed) deleteConnection(c);
        m_menuConnections.clear();
    }
}

bool NodeGraph::beginNodeDropdown(const char* id, const char* preview, ImGuiComboFlags flags) {
    // The list is a window of its own, opened while the canvas has the font's UI scale switched off (see
    // render()): it takes the graph's zoom (which already includes the UI scale) for its text, so it stays
    // the size of the node's own text, and gets a little room above and below.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().WindowPadding.x, 5.0f * m_viewZoom));
    const bool open = ImGui::BeginCombo(id, preview, flags);
    ImGui::PopStyleVar();
    if (open) ImGui::SetWindowFontScale(m_viewZoom);
    return open;
}

ImVec2 NodeGraph::getPinScreenPosition(const Node* node, const NodePin& pin, const ImVec2& canvasPos) const {
    float scaledGridSize = m_gridSize * m_viewZoom;
    ImVec2 nodeScreenPos = graphToScreen(
        ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize), canvasPos);
    
    // Pin layout parameters (must match renderNodes EXACTLY)
    float headerHeight = m_gridSize * m_viewZoom;  // 1 grid cell for header
    float pinSpacing = m_gridSize * m_viewZoom;    // 1 grid cell between pins
    float pinInset = 0.5f * scaledGridSize;        // Pins are inset 0.5 grid units from node edge
    float pinStartY = nodeScreenPos.y + headerHeight + pinSpacing * 0.5f;
    
    ImVec2 nodeSize = ImVec2(node->size.x * scaledGridSize, node->size.y * scaledGridSize);
    
    if (pin.isOutput) {
        // Find the output pin index
        int pinIndex = 0;
        for (size_t i = 0; i < node->outputs.size(); i++) {
            if (node->outputs[i].id == pin.id) {
                pinIndex = (int)i;
                break;
            }
        }
        float pinY = pinStartY + pinIndex * pinSpacing;
        return ImVec2(nodeScreenPos.x + nodeSize.x - pinInset, pinY);
    } else {
        // Find the input pin index
        int pinIndex = 0;
        for (size_t i = 0; i < node->inputs.size(); i++) {
            if (node->inputs[i].id == pin.id) {
                pinIndex = (int)i;
                break;
            }
        }
        float pinY = pinStartY + pinIndex * pinSpacing;
        return ImVec2(nodeScreenPos.x + pinInset, pinY);
    }
}

NodeGraph::PinHitResult NodeGraph::hitTestPin(const ImVec2& mousePos, const ImVec2& canvasPos) const {
    PinHitResult result;
    float hitRadius = 10.0f * m_viewZoom;  // Slightly larger than visual pin for easier clicking
    
    // Get nodes sorted by z-order (lowest first)
    std::vector<Node*> sortedNodes = getNodesSortedByZOrder();
    
    // Find the topmost node at the mouse position to determine occlusion
    Node* topmostNode = getTopmostNodeAt(mousePos, canvasPos);
    
    // Iterate through nodes in reverse z-order (highest first) to find pins
    for (auto it = sortedNodes.rbegin(); it != sortedNodes.rend(); ++it) {
        Node* node = *it;
        
        // If there's a topmost node at this position and this node is not that node,
        // and this node's z-order is lower, skip it (it's occluded)
        // Exception: if we're currently dragging THIS node, allow its pins
        if (topmostNode && node != topmostNode && node->zOrder < topmostNode->zOrder) {
            // Check if the pin position is inside the occluding node
            // (pins might extend outside their node's body)
            bool anyPinOccluded = false;
            
            // Check if mouse is actually inside another node that's on top
            if (isPointInsideNode(topmostNode, mousePos, canvasPos)) {
                // Mouse is inside a higher z-order node, skip this lower node's pins
                continue;
            }
        }
        
        // Check input pins
        for (const auto& pin : node->inputs) {
            ImVec2 pinPos = getPinScreenPosition(node, pin, canvasPos);
            float dx = mousePos.x - pinPos.x;
            float dy = mousePos.y - pinPos.y;
            if (dx * dx + dy * dy <= hitRadius * hitRadius) {
                // Make sure this pin position isn't occluded by a higher z-order node
                Node* pinOccluder = getTopmostNodeAt(pinPos, canvasPos);
                if (!pinOccluder || pinOccluder == node || pinOccluder->zOrder <= node->zOrder) {
                    result.node = node;
                    result.pin = const_cast<NodePin*>(&pin);
                    result.nodeId = node->id;
                    result.pinId = pin.id;
                    return result;
                }
            }
        }
        
        // Check output pins
        for (const auto& pin : node->outputs) {
            ImVec2 pinPos = getPinScreenPosition(node, pin, canvasPos);
            float dx = mousePos.x - pinPos.x;
            float dy = mousePos.y - pinPos.y;
            if (dx * dx + dy * dy <= hitRadius * hitRadius) {
                // Make sure this pin position isn't occluded by a higher z-order node
                Node* pinOccluder = getTopmostNodeAt(pinPos, canvasPos);
                if (!pinOccluder || pinOccluder == node || pinOccluder->zOrder <= node->zOrder) {
                    result.node = node;
                    result.pin = const_cast<NodePin*>(&pin);
                    result.nodeId = node->id;
                    result.pinId = pin.id;
                    return result;
                }
            }
        }
    }
    
    return result;
}

bool NodeGraph::canConnect(const Node* fromNode, const NodePin& fromPin, const Node* toNode, const NodePin& toPin) const {
    // Can't connect a node to itself
    if (fromNode->id == toNode->id) return false;
    
    // Can only connect output to input (or input to output if dragging from input)
    if (fromPin.isOutput == toPin.isOutput) return false;
    
    // For now, restrict connections:
    // Color Wheel (ColorWheel type) can only connect to Base Color input (pin id 1) of Final Material
    
    // Script nodes: only execution order can be wired by hand (values are plugged in from a menu),
    // and only from an output to an input that is also an execution pin.
    if (dynamic_cast<const ScriptNode*>(fromNode) || dynamic_cast<const ScriptNode*>(toNode)) {
        const ScriptNode* a = dynamic_cast<const ScriptNode*>(fromNode);
        const ScriptNode* b = dynamic_cast<const ScriptNode*>(toNode);
        if (!a || !b) return false;
        // A Get node's output can go into a value input (not an execution pin, not another Get node).
        const ScriptNode* outNode = fromPin.isOutput ? a : b;
        const ScriptNode* inNode = fromPin.isOutput ? b : a;
        if (outNode->carriesVariable()) return !inNode->carriesVariable() && fromPin.type != PinType::Exec && toPin.type != PinType::Exec;
        // So can the output of a value node that is plugged into nothing yet.
        if (outNode->isData() && outNode->floating) return !inNode->isData() && fromPin.type != PinType::Exec && toPin.type != PinType::Exec;
        return fromPin.type == PinType::Exec && toPin.type == PinType::Exec;
    }

    // Determine which is output and which is input
    const Node* outputNode = fromPin.isOutput ? fromNode : toNode;
    const NodePin& outputPin = fromPin.isOutput ? fromPin : toPin;
    const Node* inputNode = fromPin.isOutput ? toNode : fromNode;
    const NodePin& inputPin = fromPin.isOutput ? toPin : fromPin;
    
    // If connecting from a ColorWheel node
    if (outputNode->type == "ColorWheel") {
        // Can only connect to MaterialOutput's Base Color input (pin id 1)
        if (inputNode->type == "MaterialOutput" && inputPin.id == 1) {
            return true;
        }
        // Cannot connect to any other pins
        return false;
    }
    
    // Default: allow connection if pin types match (or either is Any)
    if (outputPin.type == toPin.type || outputPin.type == PinType::Any || toPin.type == PinType::Any) {
        return true;
    }
    
    return false;
}

bool NodeGraph::isPinConnectable(const Node* node, const NodePin& pin) const {
    // If not dragging a pin, all pins are "connectable" (not greyed out)
    if (!m_isDraggingPin) return true;
    
    // Get the source pin info
    Node* sourceNode = const_cast<NodeGraph*>(this)->getNode(m_draggingPinNodeId);
    if (!sourceNode) return true;
    
    // Find the source pin
    const NodePin* sourcePin = nullptr;
    if (m_draggingPinIsOutput) {
        for (const auto& p : sourceNode->outputs) {
            if (p.id == m_draggingPinId) {
                sourcePin = &p;
                break;
            }
        }
    } else {
        for (const auto& p : sourceNode->inputs) {
            if (p.id == m_draggingPinId) {
                sourcePin = &p;
                break;
            }
        }
    }
    
    if (!sourcePin) return true;
    
    // The source pin itself should remain highlighted (not greyed out)
    if (node->id == m_draggingPinNodeId && pin.id == m_draggingPinId) {
        return true;
    }
    
    // Check if this pin can connect to the source pin
    return canConnect(sourceNode, *sourcePin, node, pin);
}

// renderDraggingConnection moved to src/nodegraph/ConnectionRenderer.cpp

void NodeGraph::setAvailableTextures(const std::vector<TextureInfo>& textures) {
    m_availableTextures = textures;
    
    // Update selectedTextureIndex for all Image nodes based on their selectedTexturePath
    for (auto& node : m_nodes) {
        ImageNode* imageNode = dynamic_cast<ImageNode*>(node.get());
        if (!imageNode) continue;
        
        if (imageNode->selectedTexturePath.empty()) {
            imageNode->selectedTextureIndex = -1;
        } else {
            // Find the texture index by matching path
            imageNode->selectedTextureIndex = -1;
            for (size_t i = 0; i < m_availableTextures.size(); i++) {
                if (m_availableTextures[i].filepath == imageNode->selectedTexturePath) {
                    imageNode->selectedTextureIndex = static_cast<int>(i);
                    break;
                }
            }
        }
    }
}

// Image node rendering functions moved to src/nodegraph/ImageNodeRenderer.cpp

// Clipboard functions moved to src/nodegraph/NodeGraphClipboard.cpp

void NodeGraph::bringNodeToFront(Node* node) {
    if (node) {
        node->zOrder = m_nextZOrder++;
    }
}

std::vector<Node*> NodeGraph::getNodesSortedByZOrder() const {
    std::vector<Node*> sortedNodes;
    sortedNodes.reserve(m_nodes.size());
    
    for (const auto& node : m_nodes) {
        sortedNodes.push_back(node.get());
    }
    
    // Sort by z-order (lower values rendered first, higher values on top)
    std::sort(sortedNodes.begin(), sortedNodes.end(), [](const Node* a, const Node* b) {
        return a->zOrder < b->zOrder;
    });
    
    return sortedNodes;
}

bool NodeGraph::isPointInsideNode(const Node* node, const ImVec2& screenPos, const ImVec2& canvasPos) const {
    float scaledGridSize = m_gridSize * m_viewZoom;
    ImVec2 nodePixelPos = ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize);
    ImVec2 nodeScreenPos = graphToScreen(nodePixelPos, canvasPos);
    ImVec2 nodeSize = ImVec2(node->size.x * scaledGridSize, node->size.y * scaledGridSize);
    
    return (screenPos.x >= nodeScreenPos.x && screenPos.x <= nodeScreenPos.x + nodeSize.x &&
            screenPos.y >= nodeScreenPos.y && screenPos.y <= nodeScreenPos.y + nodeSize.y);
}

Node* NodeGraph::getTopmostNodeAt(const ImVec2& screenPos, const ImVec2& canvasPos) const {
    // Get nodes sorted by z-order (lowest first), then iterate backwards to find topmost
    std::vector<Node*> sortedNodes = getNodesSortedByZOrder();
    
    for (auto it = sortedNodes.rbegin(); it != sortedNodes.rend(); ++it) {
        if (isPointInsideNode(*it, screenPos, canvasPos)) {
            return *it;
        }
    }
    
    return nullptr;
}

} // namespace Propulsion

namespace Propulsion {

void NodeGraph::clear() {
    m_connections.clear();
    m_nodes.clear();
    m_selectedNodeId = -1;
    m_hoveredNodeId = -1;
    m_draggingNodeId = -1;
    m_isDraggingNode = false;
    m_isDraggingPin = false;
    m_isBoxSelecting = false;
    m_dragStart.clear();
    m_dragMoved = false;
    m_hoveredPinNodeId = -1;
    m_hoveredPinId = -1;
    m_undoStack.clear();
    m_redoStack.clear();
    m_undoReady = false;
}

void NodeGraph::centerOnNode(int nodeId) {
    Node* n = getNode(nodeId);
    if (!n || m_viewZoom <= 0.0f) return;
    const float x = (n->position.x + n->size.x * 0.5f) * m_gridSize;
    const float y = (n->position.y + n->size.y * 0.5f) * m_gridSize;
    m_viewOffset = ImVec2(m_canvasSize.x * 0.5f / m_viewZoom - x, m_canvasSize.y * 0.5f / m_viewZoom - y);
}

bool NodeGraph::isNodeInView(int nodeId) const {
    const Node* n = nullptr;
    for (const auto& node : m_nodes) {
        if (node->id == nodeId) n = node.get();
    }
    if (!n) return false;
    const float x = ((n->position.x + n->size.x * 0.5f) * m_gridSize + m_viewOffset.x) * m_viewZoom;
    const float y = ((n->position.y + n->size.y * 0.5f) * m_gridSize + m_viewOffset.y) * m_viewZoom;
    return x >= 0.0f && y >= 0.0f && x <= m_canvasSize.x && y <= m_canvasSize.y;
}

void NodeGraph::selectNode(int nodeId) {
    m_selectedNodeId = nodeId;
    for (auto& n : m_nodes) {
        n->selected = (n->id == nodeId);
    }
}

void NodeGraph::fitView(const ImVec2& canvasSize) {
    if (m_nodes.empty() || canvasSize.x <= 0 || canvasSize.y <= 0) return;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& n : m_nodes) {
        float rows = static_cast<float>((std::max)(n->inputs.size(), n->outputs.size()));
        minX = (std::min)(minX, n->position.x * m_gridSize);
        minY = (std::min)(minY, n->position.y * m_gridSize);
        maxX = (std::max)(maxX, (n->position.x + n->size.x) * m_gridSize);
        maxY = (std::max)(maxY, (n->position.y + 1.5f + rows) * m_gridSize);
    }
    const float uiScale = PropulsionMKI::UI::getUIScale();
    const float padding = 40.0f * uiScale;
    float width = (std::max)(maxX - minX, 1.0f);
    float height = (std::max)(maxY - minY, 1.0f);
    float zoom = (std::min)((canvasSize.x - padding * 2) / width, (canvasSize.y - padding * 2) / height);
    m_userZoom = std::clamp(zoom / uiScale, 0.3f, 1.0f);
    m_viewZoom = m_userZoom * uiScale;
    m_viewOffset.x = padding / m_viewZoom - minX;
    m_viewOffset.y = padding / m_viewZoom - minY;
}

} // namespace Propulsion
