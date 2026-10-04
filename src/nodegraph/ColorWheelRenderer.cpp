/**
 * ColorWheelRenderer.cpp
 * Color wheel node rendering and interaction for NodeGraph
 * 
 * Extracted from NodeGraph.cpp for better maintainability
 */

#include "nodegraph/NodeGraph.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace Forge {

void NodeGraph::renderColorWheelContent(ImDrawList* drawList, ColorWheelNode* node, const ImVec2& contentPos, const ImVec2& contentSize) {
    // Color wheel layout:
    // - Left side: Color wheel with saturation/value square
    // - Below wheel: Color preview bar
    // - Bottom: Single row with HEX and RGB inputs
    
    float padding = 8.0f * m_viewZoom;  // Horizontal padding for all elements
    
    // Calculate wheel size - leave room for preview bar and one row of inputs
    float reservedForInputs = 40.0f * m_viewZoom;  // Preview bar + input row
    float availableHeight = contentSize.y - padding * 3 - reservedForInputs;
    float wheelSize = std::min(contentSize.x - padding * 2, availableHeight) * 0.95f;
    
    if (wheelSize < 20.0f) return;  // Too small to render
    
    // Left-justify the wheel with padding
    ImVec2 wheelCenter = ImVec2(contentPos.x + padding + wheelSize * 0.5f, contentPos.y + padding + wheelSize * 0.5f);
    float outerRadius = wheelSize * 0.5f;
    float innerRadius = outerRadius * 0.7f;
    
    // Draw hue wheel (outer ring)
    const int segments = 64;
    for (int i = 0; i < segments; i++) {
        float angle1 = (i / (float)segments) * 2.0f * 3.14159f;
        float angle2 = ((i + 1) / (float)segments) * 2.0f * 3.14159f;
        
        float hue1 = (i / (float)segments) * 360.0f;
        float hue2 = ((i + 1) / (float)segments) * 360.0f;
        
        // Convert hue to RGB (at full saturation and value)
        auto hueToRgb = [](float h) -> ImU32 {
            float c = 1.0f;
            float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
            float r, g, b;
            if (h < 60.0f)       { r = c; g = x; b = 0; }
            else if (h < 120.0f) { r = x; g = c; b = 0; }
            else if (h < 180.0f) { r = 0; g = c; b = x; }
            else if (h < 240.0f) { r = 0; g = x; b = c; }
            else if (h < 300.0f) { r = x; g = 0; b = c; }
            else                 { r = c; g = 0; b = x; }
            return IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), 255);
        };
        
        ImU32 col1 = hueToRgb(hue1);
        ImU32 col2 = hueToRgb(hue2);
        
        // Draw segment of the hue ring
        ImVec2 p1_outer(wheelCenter.x + cosf(angle1) * outerRadius, wheelCenter.y + sinf(angle1) * outerRadius);
        ImVec2 p2_outer(wheelCenter.x + cosf(angle2) * outerRadius, wheelCenter.y + sinf(angle2) * outerRadius);
        ImVec2 p1_inner(wheelCenter.x + cosf(angle1) * innerRadius, wheelCenter.y + sinf(angle1) * innerRadius);
        ImVec2 p2_inner(wheelCenter.x + cosf(angle2) * innerRadius, wheelCenter.y + sinf(angle2) * innerRadius);
        
        drawList->AddQuadFilled(p1_inner, p1_outer, p2_outer, p2_inner, col1);
    }
    
    // Draw saturation/value square in the center
    float squareSize = innerRadius * 1.3f;
    ImVec2 squareMin(wheelCenter.x - squareSize * 0.5f, wheelCenter.y - squareSize * 0.5f);
    ImVec2 squareMax(wheelCenter.x + squareSize * 0.5f, wheelCenter.y + squareSize * 0.5f);
    
    // Background: full saturation color at current hue
    auto hueToRgbVec = [](float h) -> ImU32 {
        float c = 1.0f;
        float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
        float r, g, b;
        if (h < 60.0f)       { r = c; g = x; b = 0; }
        else if (h < 120.0f) { r = x; g = c; b = 0; }
        else if (h < 180.0f) { r = 0; g = c; b = x; }
        else if (h < 240.0f) { r = 0; g = x; b = c; }
        else if (h < 300.0f) { r = x; g = 0; b = c; }
        else                 { r = c; g = 0; b = x; }
        return IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), 255);
    };
    
    ImU32 hueColor = hueToRgbVec(node->hue);
    
    // Draw saturation gradient (left to right: white to hue color)
    drawList->AddRectFilledMultiColor(squareMin, squareMax,
        IM_COL32(255, 255, 255, 255), hueColor, hueColor, IM_COL32(255, 255, 255, 255));
    
    // Draw value gradient overlay (top to bottom: transparent to black)
    drawList->AddRectFilledMultiColor(squareMin, squareMax,
        IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255));
    
    // Draw square border
    drawList->AddRect(squareMin, squareMax, IM_COL32(60, 60, 60, 255), 0, 0, 1.0f * m_viewZoom);
    
    // Draw hue indicator on the wheel
    // Angle 0 = right side (3 o'clock), matching how the wheel segments are drawn
    float hueAngle = (node->hue / 360.0f) * 2.0f * 3.14159f;
    float indicatorRadius = (outerRadius + innerRadius) * 0.5f;
    ImVec2 hueIndicatorPos(wheelCenter.x + cosf(hueAngle) * indicatorRadius, 
                           wheelCenter.y + sinf(hueAngle) * indicatorRadius);
    drawList->AddCircleFilled(hueIndicatorPos, 5.0f * m_viewZoom, IM_COL32(255, 255, 255, 255));
    drawList->AddCircle(hueIndicatorPos, 5.0f * m_viewZoom, IM_COL32(0, 0, 0, 255), 0, 1.5f * m_viewZoom);
    
    // Draw saturation/value indicator in the square
    float svX = squareMin.x + node->saturation * (squareMax.x - squareMin.x);
    float svY = squareMax.y - node->value * (squareMax.y - squareMin.y);
    drawList->AddCircleFilled(ImVec2(svX, svY), 5.0f * m_viewZoom, IM_COL32(255, 255, 255, 255));
    drawList->AddCircle(ImVec2(svX, svY), 5.0f * m_viewZoom, IM_COL32(0, 0, 0, 255), 0, 1.5f * m_viewZoom);
    
    // Draw color preview box
    float previewY = contentPos.y + padding + wheelSize + padding;
    float previewHeight = 16.0f * m_viewZoom;
    ImVec2 previewMin(contentPos.x + padding, previewY);
    ImVec2 previewMax(contentPos.x + contentSize.x - padding, previewY + previewHeight);
    
    ImU32 previewColor = IM_COL32(
        (int)(node->color.r * 255), 
        (int)(node->color.g * 255), 
        (int)(node->color.b * 255), 255);
    drawList->AddRectFilled(previewMin, previewMax, previewColor, 3.0f * m_viewZoom);
    drawList->AddRect(previewMin, previewMax, IM_COL32(80, 80, 80, 255), 3.0f * m_viewZoom, 0, 1.0f * m_viewZoom);
    
    // Draw input fields - all on one row: # HEX | R val | G val | B val
    // Use the same font size as pin labels (globalUniformLabelScale would be ideal, but we use a good match)
    ImGui::PushID(node->id);
    
    float inputY = previewMax.y + padding;
    float inputHeight = 16.0f * m_viewZoom;
    
    ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
    float fontSize = std::max(10.0f * m_viewZoom, 8.0f);  // Match pin label size better
    float defaultFontSize = ImGui::GetFontSize();  // ImGui's current font size
    float fontScale = fontSize / defaultFontSize;  // Scale factor for InputText
    
    if (fontSize > 4.0f) {
        float totalWidth = contentSize.x - padding * 2;
        float labelWidth = 10.0f * m_viewZoom;  // Width for #, R, G, B labels
        float spacing = 3.0f * m_viewZoom;       // Space between fields
        
        // Calculate field widths: HEX gets more space, RGB fields are equal
        // Layout: # [HEX] R [val] G [val] B [val]
        // 4 labels + 4 fields + 3 spacings
        float availableForFields = totalWidth - labelWidth * 4 - spacing * 3;
        float hexFieldWidth = availableForFields * 0.4f;  // HEX gets 40%
        float rgbFieldWidth = availableForFields * 0.2f;  // Each RGB gets 20%
        
        float x = contentPos.x + padding;
        ImVec2 mousePos = ImGui::GetMousePos();
        bool isDoubleClick = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        bool isClickedOutside = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        
        // Helper lambda to check if mouse is in rect
        auto isMouseInRect = [&mousePos](const ImVec2& minP, const ImVec2& maxP) {
            return mousePos.x >= minP.x && mousePos.x <= maxP.x && 
                   mousePos.y >= minP.y && mousePos.y <= maxP.y;
        };
        
        // # label and HEX field
        drawList->AddText(font, fontSize, ImVec2(x, inputY + (inputHeight - fontSize) * 0.5f), 
                         IM_COL32(200, 200, 200, 255), "#");
        x += labelWidth;
        ImVec2 hexFieldMin(x, inputY);
        ImVec2 hexFieldMax(x + hexFieldWidth, inputY + inputHeight);
        
        bool isEditingHex = (m_editingField == EditingField::Hex && m_editingNodeId == node->id);
        if (isEditingHex) {
            // Render actual input field
            drawList->AddRectFilled(hexFieldMin, hexFieldMax, IM_COL32(60, 60, 60, 255), 2.0f * m_viewZoom);
            drawList->AddRect(hexFieldMin, hexFieldMax, IM_COL32(100, 150, 255, 255), 2.0f * m_viewZoom, 0, 2.0f * m_viewZoom);
            
            ImGui::SetWindowFontScale(fontScale);
            ImGui::SetCursorScreenPos(ImVec2(hexFieldMin.x + 2.0f * m_viewZoom, hexFieldMin.y + (inputHeight - fontSize) * 0.5f));
            ImGui::PushItemWidth(hexFieldWidth - 4.0f * m_viewZoom);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 220, 255));
            
            if (m_editingFrameCount == 0) {
                ImGui::SetKeyboardFocusHere();
            }
            
            ImGui::PushID("HexInput");
            if (ImGui::InputText("##hex", node->hexBuffer, sizeof(node->hexBuffer), 
                ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | 
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                // Enter pressed - apply and stop editing
                node->updateColorFromHex();
                m_editingField = EditingField::None;
                m_editingNodeId = -1;
                m_hasUnsavedChanges = true;
            }
            // Only check for lost focus after a few frames to let ImGui activate the input
            if (m_editingFrameCount > 2 && !ImGui::IsItemActive()) {
                // Lost focus - apply and stop editing
                node->updateColorFromHex();
                m_editingField = EditingField::None;
                m_editingNodeId = -1;
                m_hasUnsavedChanges = true;
            }
            m_editingFrameCount++;
            ImGui::PopID();
            
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            ImGui::PopItemWidth();
            ImGui::SetWindowFontScale(1.0f);
        } else {
            drawList->AddRectFilled(hexFieldMin, hexFieldMax, IM_COL32(40, 40, 40, 255), 2.0f * m_viewZoom);
            drawList->AddRect(hexFieldMin, hexFieldMax, IM_COL32(70, 70, 70, 255), 2.0f * m_viewZoom, 0, 1.0f * m_viewZoom);
            drawList->AddText(font, fontSize, ImVec2(x + 2.0f * m_viewZoom, inputY + (inputHeight - fontSize) * 0.5f), 
                             IM_COL32(220, 220, 220, 255), node->hexBuffer);
            
            // Check for double-click to start editing
            if (isDoubleClick && isMouseInRect(hexFieldMin, hexFieldMax)) {
                m_editingField = EditingField::Hex;
                m_editingNodeId = node->id;
                m_editingFrameCount = 0;
            }
        }
        x += hexFieldWidth + spacing;
        
        // R label and field
        drawList->AddText(font, fontSize, ImVec2(x, inputY + (inputHeight - fontSize) * 0.5f), 
                         IM_COL32(255, 100, 100, 255), "R");
        x += labelWidth;
        ImVec2 rFieldMin(x, inputY);
        ImVec2 rFieldMax(x + rgbFieldWidth, inputY + inputHeight);
        
        bool isEditingR = (m_editingField == EditingField::R && m_editingNodeId == node->id);
        if (isEditingR) {
            drawList->AddRectFilled(rFieldMin, rFieldMax, IM_COL32(60, 60, 60, 255), 2.0f * m_viewZoom);
            drawList->AddRect(rFieldMin, rFieldMax, IM_COL32(255, 100, 100, 255), 2.0f * m_viewZoom, 0, 2.0f * m_viewZoom);
            
            ImGui::SetWindowFontScale(fontScale);
            ImGui::SetCursorScreenPos(ImVec2(rFieldMin.x + 2.0f * m_viewZoom, rFieldMin.y + (inputHeight - fontSize) * 0.5f));
            ImGui::PushItemWidth(rgbFieldWidth - 4.0f * m_viewZoom);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 220, 255));
            
            if (m_editingFrameCount == 0) {
                ImGui::SetKeyboardFocusHere();
            }
            
            ImGui::PushID("RInput");
            if (ImGui::InputText("##r", node->rBuffer, sizeof(node->rBuffer), 
                ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                node->updateColorFromRgb();
                m_editingField = EditingField::None;
                m_editingNodeId = -1;
                m_hasUnsavedChanges = true;
            }
            if (m_editingFrameCount > 2 && !ImGui::IsItemActive()) {
                node->updateColorFromRgb();
                m_editingField = EditingField::None;
                m_editingNodeId = -1;
                m_hasUnsavedChanges = true;
            }
            m_editingFrameCount++;
            ImGui::PopID();
            
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            ImGui::PopItemWidth();
            ImGui::SetWindowFontScale(1.0f);
        } else {
            drawList->AddRectFilled(rFieldMin, rFieldMax, IM_COL32(40, 40, 40, 255), 2.0f * m_viewZoom);
            drawList->AddRect(rFieldMin, rFieldMax, IM_COL32(70, 70, 70, 255), 2.0f * m_viewZoom, 0, 1.0f * m_viewZoom);
            drawList->AddText(font, fontSize, ImVec2(x + 2.0f * m_viewZoom, inputY + (inputHeight - fontSize) * 0.5f), 
                             IM_COL32(220, 220, 220, 255), node->rBuffer);
            
            if (isDoubleClick && isMouseInRect(rFieldMin, rFieldMax)) {
                m_editingField = EditingField::R;
                m_editingNodeId = node->id;
                m_editingFrameCount = 0;
            }
        }
        x += rgbFieldWidth + spacing;
        
        // G label and field
        drawList->AddText(font, fontSize, ImVec2(x, inputY + (inputHeight - fontSize) * 0.5f), 
                         IM_COL32(100, 255, 100, 255), "G");
        x += labelWidth;
        ImVec2 gFieldMin(x, inputY);
        ImVec2 gFieldMax(x + rgbFieldWidth, inputY + inputHeight);
        
        bool isEditingG = (m_editingField == EditingField::G && m_editingNodeId == node->id);
        if (isEditingG) {
            drawList->AddRectFilled(gFieldMin, gFieldMax, IM_COL32(60, 60, 60, 255), 2.0f * m_viewZoom);
            drawList->AddRect(gFieldMin, gFieldMax, IM_COL32(100, 255, 100, 255), 2.0f * m_viewZoom, 0, 2.0f * m_viewZoom);
            
            ImGui::SetWindowFontScale(fontScale);
            ImGui::SetCursorScreenPos(ImVec2(gFieldMin.x + 2.0f * m_viewZoom, gFieldMin.y + (inputHeight - fontSize) * 0.5f));
            ImGui::PushItemWidth(rgbFieldWidth - 4.0f * m_viewZoom);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 220, 255));
            
            if (m_editingFrameCount == 0) {
                ImGui::SetKeyboardFocusHere();
            }
            
            ImGui::PushID("GInput");
            if (ImGui::InputText("##g", node->gBuffer, sizeof(node->gBuffer), 
                ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                node->updateColorFromRgb();
                m_editingField = EditingField::None;
                m_editingNodeId = -1;
                m_hasUnsavedChanges = true;
            }
            if (m_editingFrameCount > 2 && !ImGui::IsItemActive()) {
                node->updateColorFromRgb();
                m_editingField = EditingField::None;
                m_editingNodeId = -1;
                m_hasUnsavedChanges = true;
            }
            m_editingFrameCount++;
            ImGui::PopID();
            
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            ImGui::PopItemWidth();
            ImGui::SetWindowFontScale(1.0f);
        } else {
            drawList->AddRectFilled(gFieldMin, gFieldMax, IM_COL32(40, 40, 40, 255), 2.0f * m_viewZoom);
            drawList->AddRect(gFieldMin, gFieldMax, IM_COL32(70, 70, 70, 255), 2.0f * m_viewZoom, 0, 1.0f * m_viewZoom);
            drawList->AddText(font, fontSize, ImVec2(x + 2.0f * m_viewZoom, inputY + (inputHeight - fontSize) * 0.5f), 
                             IM_COL32(220, 220, 220, 255), node->gBuffer);
            
            if (isDoubleClick && isMouseInRect(gFieldMin, gFieldMax)) {
                m_editingField = EditingField::G;
                m_editingNodeId = node->id;
                m_editingFrameCount = 0;
            }
        }
        x += rgbFieldWidth + spacing;
        
        // B label and field
        drawList->AddText(font, fontSize, ImVec2(x, inputY + (inputHeight - fontSize) * 0.5f), 
                         IM_COL32(100, 100, 255, 255), "B");
        x += labelWidth;
        ImVec2 bFieldMin(x, inputY);
        ImVec2 bFieldMax(x + rgbFieldWidth, inputY + inputHeight);
        
        bool isEditingB = (m_editingField == EditingField::B && m_editingNodeId == node->id);
        if (isEditingB) {
            drawList->AddRectFilled(bFieldMin, bFieldMax, IM_COL32(60, 60, 60, 255), 2.0f * m_viewZoom);
            drawList->AddRect(bFieldMin, bFieldMax, IM_COL32(100, 100, 255, 255), 2.0f * m_viewZoom, 0, 2.0f * m_viewZoom);
            
            ImGui::SetWindowFontScale(fontScale);
            ImGui::SetCursorScreenPos(ImVec2(bFieldMin.x + 2.0f * m_viewZoom, bFieldMin.y + (inputHeight - fontSize) * 0.5f));
            ImGui::PushItemWidth(rgbFieldWidth - 4.0f * m_viewZoom);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 220, 220, 255));
            
            if (m_editingFrameCount == 0) {
                ImGui::SetKeyboardFocusHere();
            }
            
            ImGui::PushID("BInput");
            if (ImGui::InputText("##b", node->bBuffer, sizeof(node->bBuffer), 
                ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                node->updateColorFromRgb();
                m_editingField = EditingField::None;
                m_editingNodeId = -1;
                m_hasUnsavedChanges = true;
            }
            if (m_editingFrameCount > 2 && !ImGui::IsItemActive()) {
                node->updateColorFromRgb();
                m_editingField = EditingField::None;
                m_editingNodeId = -1;
                m_hasUnsavedChanges = true;
            }
            m_editingFrameCount++;
            ImGui::PopID();
            
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            ImGui::PopItemWidth();
            ImGui::SetWindowFontScale(1.0f);
        } else {
            drawList->AddRectFilled(bFieldMin, bFieldMax, IM_COL32(40, 40, 40, 255), 2.0f * m_viewZoom);
            drawList->AddRect(bFieldMin, bFieldMax, IM_COL32(70, 70, 70, 255), 2.0f * m_viewZoom, 0, 1.0f * m_viewZoom);
            drawList->AddText(font, fontSize, ImVec2(x + 2.0f * m_viewZoom, inputY + (inputHeight - fontSize) * 0.5f), 
                             IM_COL32(220, 220, 220, 255), node->bBuffer);
            
            if (isDoubleClick && isMouseInRect(bFieldMin, bFieldMax)) {
                m_editingField = EditingField::B;
                m_editingNodeId = node->id;
                m_editingFrameCount = 0;
            }
        }
    }
    
    ImGui::PopID();
}

NodeGraph::ColorWheelHitArea NodeGraph::hitTestColorWheel(ColorWheelNode* node, const ImVec2& mousePos, const ImVec2& canvasPos) const {
    // Calculate the same layout as renderColorWheelContent (must match EXACTLY)
    float scaledGridSize = m_gridSize * m_viewZoom;
    ImVec2 nodeScreenPos = graphToScreen(
        ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize), canvasPos);
    
    float headerHeight = m_gridSize * m_viewZoom;
    ImVec2 nodeSize = ImVec2(node->size.x * scaledGridSize, node->size.y * scaledGridSize);
    
    // Content area starts right after header (matching rendering)
    ImVec2 contentPos = ImVec2(nodeScreenPos.x, nodeScreenPos.y + headerHeight);
    ImVec2 contentSize = ImVec2(nodeSize.x, nodeSize.y - headerHeight);
    
    float padding = 8.0f * m_viewZoom;  // Must match renderColorWheelContent
    float reservedForInputs = 40.0f * m_viewZoom;
    float availableHeight = contentSize.y - padding * 3 - reservedForInputs;
    float wheelSize = std::min(contentSize.x - padding * 2, availableHeight) * 0.95f;
    
    if (wheelSize < 20.0f) return ColorWheelHitArea::None;
    
    // Left-justified wheel center (matching rendering)
    ImVec2 wheelCenter = ImVec2(contentPos.x + padding + wheelSize * 0.5f, contentPos.y + padding + wheelSize * 0.5f);
    float outerRadius = wheelSize * 0.5f;
    float innerRadius = outerRadius * 0.7f;
    
    // Check distance from wheel center
    float dx = mousePos.x - wheelCenter.x;
    float dy = mousePos.y - wheelCenter.y;
    float dist = sqrtf(dx * dx + dy * dy);
    
    // Check if in hue ring (between inner and outer radius)
    if (dist >= innerRadius && dist <= outerRadius) {
        return ColorWheelHitArea::HueRing;
    }
    
    // Check if in saturation/value square
    float squareSize = innerRadius * 1.3f;
    ImVec2 squareMin(wheelCenter.x - squareSize * 0.5f, wheelCenter.y - squareSize * 0.5f);
    ImVec2 squareMax(wheelCenter.x + squareSize * 0.5f, wheelCenter.y + squareSize * 0.5f);
    
    if (mousePos.x >= squareMin.x && mousePos.x <= squareMax.x &&
        mousePos.y >= squareMin.y && mousePos.y <= squareMax.y) {
        return ColorWheelHitArea::SatValSquare;
    }
    
    return ColorWheelHitArea::None;
}

void NodeGraph::handleColorWheelInteraction(ColorWheelNode* node, const ImVec2& mousePos, const ImVec2& canvasPos) {
    // Calculate the same layout as renderColorWheelContent (must match EXACTLY)
    float scaledGridSize = m_gridSize * m_viewZoom;
    ImVec2 nodeScreenPos = graphToScreen(
        ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize), canvasPos);
    
    float headerHeight = m_gridSize * m_viewZoom;
    ImVec2 nodeSize = ImVec2(node->size.x * scaledGridSize, node->size.y * scaledGridSize);
    
    // Content area starts right after header (matching rendering)
    ImVec2 contentPos = ImVec2(nodeScreenPos.x, nodeScreenPos.y + headerHeight);
    ImVec2 contentSize = ImVec2(nodeSize.x, nodeSize.y - headerHeight);
    
    float padding = 8.0f * m_viewZoom;  // Must match renderColorWheelContent
    float reservedForInputs = 40.0f * m_viewZoom;
    float availableHeight = contentSize.y - padding * 3 - reservedForInputs;
    float wheelSize = std::min(contentSize.x - padding * 2, availableHeight) * 0.95f;
    
    if (wheelSize < 20.0f) return;
    
    // Left-justified wheel center (matching rendering)
    ImVec2 wheelCenter = ImVec2(contentPos.x + padding + wheelSize * 0.5f, contentPos.y + padding + wheelSize * 0.5f);
    float outerRadius = wheelSize * 0.5f;
    float innerRadius = outerRadius * 0.7f;
    
    if (m_isDraggingHue) {
        // Update hue based on angle from center
        // atan2 returns angle where 0 = right (3 o'clock), matching our wheel rendering
        float dx = mousePos.x - wheelCenter.x;
        float dy = mousePos.y - wheelCenter.y;
        float angle = atan2f(dy, dx);
        if (angle < 0) angle += 2.0f * 3.14159f;
        node->hue = (angle / (2.0f * 3.14159f)) * 360.0f;
        node->hsvToRgb();
        node->updateHexFromColor();
        node->updateRgbBuffersFromColor();
        m_hasUnsavedChanges = true;
    }
    
    if (m_isDraggingSV) {
        // Update saturation/value based on position in square
        float squareSize = innerRadius * 1.3f;
        ImVec2 squareMin(wheelCenter.x - squareSize * 0.5f, wheelCenter.y - squareSize * 0.5f);
        ImVec2 squareMax(wheelCenter.x + squareSize * 0.5f, wheelCenter.y + squareSize * 0.5f);
        
        // Clamp mouse to square bounds
        float clampedX = std::max(squareMin.x, std::min(mousePos.x, squareMax.x));
        float clampedY = std::max(squareMin.y, std::min(mousePos.y, squareMax.y));
        
        node->saturation = (clampedX - squareMin.x) / (squareMax.x - squareMin.x);
        node->value = 1.0f - (clampedY - squareMin.y) / (squareMax.y - squareMin.y);
        node->hsvToRgb();
        node->updateHexFromColor();
        node->updateRgbBuffersFromColor();
        m_hasUnsavedChanges = true;
    }
}

} // namespace Forge
