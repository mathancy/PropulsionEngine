/**
 * ImageNodeRenderer.cpp
 * Image node rendering and interaction for NodeGraph
 * 
 * Extracted from NodeGraph.cpp for better maintainability
 */

#include "nodegraph/NodeGraph.h"
#include <imgui.h>
#include <algorithm>

namespace Forge {

bool NodeGraph::renderSingleImageNodeWidgets(ImageNode* imageNode, const ImVec2& canvasPos) {
    // Render ImGui widgets for a single Image node
    // Returns true if any widget is hovered or active
    
    bool anyWidgetActive = false;
    float scaledGridSize = m_gridSize * m_viewZoom;
    float headerHeight = m_gridSize * m_viewZoom;
    float padding = 8.0f * m_viewZoom;
    float rowHeight = 24.0f * m_viewZoom;
    float pinAreaHeight = m_gridSize * m_viewZoom;
    
    ImVec2 nodePixelPos = ImVec2(imageNode->position.x * m_gridSize, imageNode->position.y * m_gridSize);
    ImVec2 nodeScreenPos = graphToScreen(nodePixelPos, canvasPos);
    ImVec2 nodeSize = ImVec2(imageNode->size.x * scaledGridSize, imageNode->size.y * scaledGridSize);
    
    // Content area starts after header
    ImVec2 contentPos = ImVec2(nodeScreenPos.x, nodeScreenPos.y + headerHeight);
    ImVec2 contentSize = ImVec2(nodeSize.x, nodeSize.y - headerHeight);
    
    // Calculate positions for widgets
    float currentY = contentPos.y + pinAreaHeight + padding;
    float contentWidth = contentSize.x - padding * 2;
    
    // Push a clip rect for this node's bounds
    ImVec2 clipMin = nodeScreenPos;
    ImVec2 clipMax = ImVec2(nodeScreenPos.x + nodeSize.x, nodeScreenPos.y + nodeSize.y);
    ImGui::PushClipRect(clipMin, clipMax, true);
    
    ImGui::PushID(imageNode->id);
    
    // Scale font and style for zoom level
    // We need to adjust ImGui's frame padding and grab size to match our zoom
    ImGui::SetWindowFontScale(m_viewZoom);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * m_viewZoom, 2.0f * m_viewZoom));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * m_viewZoom, 2.0f * m_viewZoom));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 10.0f * m_viewZoom);  // Scale slider grab width
    
    // === Texture Type Dropdown ===
    ImGui::SetCursorScreenPos(ImVec2(contentPos.x + padding, currentY));
    ImGui::PushItemWidth(contentWidth);
    
    const char* currentTypeName = getTextureTypeName(imageNode->textureType);
    std::string comboLabel1 = "##TextureType" + std::to_string(imageNode->id);
    
    if (beginNodeDropdown(comboLabel1.c_str(), currentTypeName, ImGuiComboFlags_HeightRegular)) {
        anyWidgetActive = true;
        for (int i = 0; i < static_cast<int>(TextureType::Count); i++) {
            TextureType type = static_cast<TextureType>(i);
            bool isSelected = (imageNode->textureType == type);
            
            if (ImGui::Selectable(getTextureTypeName(type), isSelected)) {
                imageNode->textureType = type;
                imageNode->updateOutputPinType();
                m_hasUnsavedChanges = true;
            }
            
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        anyWidgetActive = true;
    }
    
    currentY += rowHeight + padding;
    
    // === Texture Selection Dropdown ===
    ImGui::SetCursorScreenPos(ImVec2(contentPos.x + padding, currentY));
    
    std::string currentTextureName = "None";
    if (imageNode->selectedTextureIndex >= 0 && 
        imageNode->selectedTextureIndex < static_cast<int>(m_availableTextures.size())) {
        currentTextureName = m_availableTextures[imageNode->selectedTextureIndex].name;
    }
    
    std::string comboLabel2 = "##TextureSelect" + std::to_string(imageNode->id);
    if (beginNodeDropdown(comboLabel2.c_str(), currentTextureName.c_str(), ImGuiComboFlags_HeightLarge)) {
        anyWidgetActive = true;
        
        float dropdownItemHeight = 36.0f * m_viewZoom;
        float previewSize = 28.0f * m_viewZoom;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // "None" option
        bool isNoneSelected = (imageNode->selectedTextureIndex == -1);
        if (ImGui::Selectable("None", isNoneSelected, 0, ImVec2(0, dropdownItemHeight))) {
            imageNode->selectedTextureIndex = -1;
            imageNode->selectedTexturePath.clear();
            m_hasUnsavedChanges = true;
        }
        if (isNoneSelected) {
            ImGui::SetItemDefaultFocus();
        }
        
        // List all available textures with thumbnails
        for (int i = 0; i < static_cast<int>(m_availableTextures.size()); i++) {
            const TextureInfo& tex = m_availableTextures[i];
            bool isSelected = (imageNode->selectedTextureIndex == i);
            
            ImGui::PushID(i);
            
            ImVec2 selectablePos = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##texsel", isSelected, 0, ImVec2(0, dropdownItemHeight))) {
                imageNode->selectedTextureIndex = i;
                imageNode->selectedTexturePath = tex.filepath;
                m_hasUnsavedChanges = true;
            }
            
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
            
            // Draw thumbnail
            ImGui::SameLine(0, 0);
            ImGui::SetCursorScreenPos(ImVec2(selectablePos.x + 4.0f * m_viewZoom, selectablePos.y + 4.0f * m_viewZoom));
            
            if (tex.thumbnail != VK_NULL_HANDLE) {
                ImGui::Image((ImTextureID)tex.thumbnail, ImVec2(previewSize, previewSize));
            } else {
                ImVec2 placeholderPos = ImGui::GetCursorScreenPos();
                drawList->AddRectFilled(placeholderPos, 
                    ImVec2(placeholderPos.x + previewSize, placeholderPos.y + previewSize),
                    IM_COL32(80, 80, 80, 255));
                ImGui::Dummy(ImVec2(previewSize, previewSize));
            }
            
            // Draw texture name
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (previewSize - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::Text("%s", tex.name.c_str());
            
            ImGui::PopID();
        }
        
        ImGui::EndCombo();
    }
    
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        anyWidgetActive = true;
    }
    
    currentY += rowHeight + padding;
    
    // === Invert Checkbox (for Normal, Metallic, Roughness, AO texture types) ===
    if (imageNode->textureType == TextureType::Normal ||
        imageNode->textureType == TextureType::Metallic ||
        imageNode->textureType == TextureType::Roughness ||
        imageNode->textureType == TextureType::AmbientOcclusion) {
        
        ImGui::SetCursorScreenPos(ImVec2(contentPos.x + padding, currentY));
        
        std::string checkboxLabel = "Invert##" + std::to_string(imageNode->id);
        if (ImGui::Checkbox(checkboxLabel.c_str(), &imageNode->invertTexture)) {
            m_hasUnsavedChanges = true;
        }
        
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            anyWidgetActive = true;
        }
        
        currentY += rowHeight + padding;
    }
    
    // === Strength Slider (for all texture types except Albedo) ===
    if (imageNode->textureType != TextureType::Albedo) {
        ImGui::SetCursorScreenPos(ImVec2(contentPos.x + padding, currentY));
        
        // Draw "S" label
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "S");
        
        // Slider on same row after the label
        ImGui::SameLine();
        float labelWidth = ImGui::CalcTextSize("S").x + 8.0f * m_viewZoom;
        float sliderWidth = contentWidth - labelWidth;
        ImGui::PushItemWidth(sliderWidth);
        
        std::string sliderLabel = "##Strength" + std::to_string(imageNode->id);
        if (ImGui::SliderFloat(sliderLabel.c_str(), &imageNode->strength, 0.0f, 2.0f, "%.2f")) {
            m_hasUnsavedChanges = true;
        }
        
        ImGui::PopItemWidth();
        
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            anyWidgetActive = true;
        }
    }
    
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(3);  // Pop FramePadding, ItemSpacing, and GrabMinSize
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopID();
    ImGui::PopClipRect();
    
    return anyWidgetActive;
}

bool NodeGraph::renderImageNodeWidgets(const ImVec2& canvasPos) {
    // Render interactive ImGui widgets for Image nodes BEFORE the canvas InvisibleButton
    // This allows the widgets to capture mouse input
    // Returns true if any widget is hovered or active (combo popup open, etc.)
    
    bool anyWidgetActive = false;
    float scaledGridSize = m_gridSize * m_viewZoom;
    float headerHeight = m_gridSize * m_viewZoom;
    float padding = 8.0f * m_viewZoom;
    float rowHeight = 24.0f * m_viewZoom;
    float pinAreaHeight = m_gridSize * m_viewZoom;  // Space for output pin
    
    // Get nodes sorted by z-order for correct rendering order
    std::vector<Node*> sortedNodes = getNodesSortedByZOrder();
    
    // Collect all image nodes with their screen rects
    struct ImageNodeInfo {
        ImageNode* node;
        ImVec2 screenPos;
        ImVec2 screenSize;
        int zOrder;
    };
    std::vector<ImageNodeInfo> imageNodes;
    
    for (auto* node : sortedNodes) {
        ImageNode* imageNode = dynamic_cast<ImageNode*>(node);
        if (!imageNode) continue;
        
        ImVec2 nodePixelPos = ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize);
        ImVec2 nodeScreenPos = graphToScreen(nodePixelPos, canvasPos);
        ImVec2 nodeSize = ImVec2(node->size.x * scaledGridSize, node->size.y * scaledGridSize);
        
        imageNodes.push_back({imageNode, nodeScreenPos, nodeSize, node->zOrder});
    }
    
    // Render widgets for each image node, using clip rects to prevent
    // lower z-order node widgets from drawing over higher z-order nodes
    for (size_t idx = 0; idx < imageNodes.size(); idx++) {
        auto& info = imageNodes[idx];
        ImageNode* imageNode = info.node;
        ImVec2 nodeScreenPos = info.screenPos;
        ImVec2 nodeSize = info.screenSize;
        
        // Content area starts after header
        ImVec2 contentPos = ImVec2(nodeScreenPos.x, nodeScreenPos.y + headerHeight);
        ImVec2 contentSize = ImVec2(nodeSize.x, nodeSize.y - headerHeight);
        
        // Calculate positions for widgets
        float currentY = contentPos.y + pinAreaHeight + padding;
        float contentWidth = contentSize.x - padding * 2;
        
        // Push a clip rect that is the node's bounds
        // This prevents this node's widgets from rendering outside its bounds
        ImVec2 clipMin = nodeScreenPos;
        ImVec2 clipMax = ImVec2(nodeScreenPos.x + nodeSize.x, nodeScreenPos.y + nodeSize.y);
        ImGui::PushClipRect(clipMin, clipMax, true);
        
        ImGui::PushID(imageNode->id);
        
        // Set font scale for node graph zoom
        ImGui::SetWindowFontScale(m_viewZoom);
        
        // === Texture Type Dropdown ===
        ImGui::SetCursorScreenPos(ImVec2(contentPos.x + padding, currentY));
        ImGui::PushItemWidth(contentWidth);
        
        const char* currentTypeName = getTextureTypeName(imageNode->textureType);
        std::string comboLabel1 = "##TextureType" + std::to_string(imageNode->id);
        
        if (beginNodeDropdown(comboLabel1.c_str(), currentTypeName, ImGuiComboFlags_HeightRegular)) {
            anyWidgetActive = true;  // Combo popup is open
            for (int i = 0; i < static_cast<int>(TextureType::Count); i++) {
                TextureType type = static_cast<TextureType>(i);
                bool isSelected = (imageNode->textureType == type);
                
                if (ImGui::Selectable(getTextureTypeName(type), isSelected)) {
                    imageNode->textureType = type;
                    imageNode->updateOutputPinType();
                    m_hasUnsavedChanges = true;
                }
                
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        // Check if this combo is hovered
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            anyWidgetActive = true;
        }
        
        currentY += rowHeight + padding;
        
        // === Texture Selection Dropdown ===
        ImGui::SetCursorScreenPos(ImVec2(contentPos.x + padding, currentY));
        
        // Get current texture name
        std::string currentTextureName = "None";
        if (imageNode->selectedTextureIndex >= 0 && 
            imageNode->selectedTextureIndex < static_cast<int>(m_availableTextures.size())) {
            currentTextureName = m_availableTextures[imageNode->selectedTextureIndex].name;
        }
        
        std::string comboLabel2 = "##TextureSelect" + std::to_string(imageNode->id);
        if (beginNodeDropdown(comboLabel2.c_str(), currentTextureName.c_str(), ImGuiComboFlags_HeightLarge)) {
            anyWidgetActive = true;  // Combo popup is open
            
            float dropdownItemHeight = 36.0f * m_viewZoom;
            float previewSize = 28.0f * m_viewZoom;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            // "None" option
            bool isNoneSelected = (imageNode->selectedTextureIndex == -1);
            if (ImGui::Selectable("None", isNoneSelected, 0, ImVec2(0, dropdownItemHeight))) {
                imageNode->selectedTextureIndex = -1;
                imageNode->selectedTexturePath.clear();
                m_hasUnsavedChanges = true;
            }
            if (isNoneSelected) {
                ImGui::SetItemDefaultFocus();
            }
            
            // List all available textures with thumbnails
            for (int i = 0; i < static_cast<int>(m_availableTextures.size()); i++) {
                const TextureInfo& tex = m_availableTextures[i];
                bool isSelected = (imageNode->selectedTextureIndex == i);
                
                ImGui::PushID(i);
                
                ImVec2 selectablePos = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##texsel", isSelected, 0, ImVec2(0, dropdownItemHeight))) {
                    imageNode->selectedTextureIndex = i;
                    imageNode->selectedTexturePath = tex.filepath;
                    m_hasUnsavedChanges = true;
                }
                
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
                
                // Draw thumbnail
                ImGui::SameLine(0, 0);
                ImGui::SetCursorScreenPos(ImVec2(selectablePos.x + 4.0f * m_viewZoom, selectablePos.y + 4.0f * m_viewZoom));
                
                if (tex.thumbnail != VK_NULL_HANDLE) {
                    ImGui::Image((ImTextureID)tex.thumbnail, ImVec2(previewSize, previewSize));
                } else {
                    ImVec2 placeholderPos = ImGui::GetCursorScreenPos();
                    drawList->AddRectFilled(placeholderPos, 
                        ImVec2(placeholderPos.x + previewSize, placeholderPos.y + previewSize),
                        IM_COL32(80, 80, 80, 255));
                    ImGui::Dummy(ImVec2(previewSize, previewSize));
                }
                
                // Draw texture name
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (previewSize - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::Text("%s", tex.name.c_str());
                
                ImGui::PopID();
            }
            
            ImGui::EndCombo();
        }
        
        // Check if this combo is hovered
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            anyWidgetActive = true;
        }
        
        currentY += rowHeight + padding;
        
        // === Invert Checkbox (for Normal, Metallic, Roughness, AO texture types) ===
        // Row 3: Invert checkbox on its own row
        if (imageNode->textureType == TextureType::Normal ||
            imageNode->textureType == TextureType::Metallic ||
            imageNode->textureType == TextureType::Roughness ||
            imageNode->textureType == TextureType::AmbientOcclusion) {
            
            ImGui::SetCursorScreenPos(ImVec2(contentPos.x + padding, currentY));
            
            std::string checkboxLabel = "Invert##" + std::to_string(imageNode->id);
            if (ImGui::Checkbox(checkboxLabel.c_str(), &imageNode->invertTexture)) {
                m_hasUnsavedChanges = true;
            }
            
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                anyWidgetActive = true;
            }
            
            currentY += rowHeight + padding;
        }
        
        // === Strength Slider (for all texture types except Albedo) ===
        // Row 4: "S" label + slider on same row
        if (imageNode->textureType != TextureType::Albedo) {
            ImGui::SetCursorScreenPos(ImVec2(contentPos.x + padding, currentY));
            
            // Draw "S" label
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "S");
            
            // Slider on same row after the label
            ImGui::SameLine();
            float labelWidth = ImGui::CalcTextSize("S").x + 8.0f * m_viewZoom;
            float sliderWidth = contentWidth - labelWidth;
            ImGui::PushItemWidth(sliderWidth);
            
            std::string sliderLabel = "##Strength" + std::to_string(imageNode->id);
            if (ImGui::SliderFloat(sliderLabel.c_str(), &imageNode->strength, 0.0f, 2.0f, "%.2f")) {
                m_hasUnsavedChanges = true;
            }
            
            ImGui::PopItemWidth();
            
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                anyWidgetActive = true;
            }
        }
        
        ImGui::PopItemWidth();
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopID();
        ImGui::PopClipRect();
    }
    
    return anyWidgetActive;
}

void NodeGraph::renderImageNodeContent(ImDrawList* drawList, ImageNode* node, const ImVec2& contentPos, const ImVec2& contentSize) {
    // Image node layout:
    // - Leave space at top for output pin (1 grid unit)
    // - Row 1: Texture type dropdown
    // - Row 2: Texture selection dropdown
    // - Row 3: Invert checkbox (non-Albedo only)
    // - Row 4: "S" + Strength slider (non-Albedo only)
    // - Image preview area (rendered here)
    
    float padding = 8.0f * m_viewZoom;
    float rowHeight = 24.0f * m_viewZoom;
    float pinAreaHeight = m_gridSize * m_viewZoom;  // Space for output pin
    
    // Calculate where the preview area starts (after pin area and dropdowns/controls)
    float currentY = contentPos.y + pinAreaHeight + padding;  // After pin area
    currentY += rowHeight + padding;  // After first dropdown (texture type)
    currentY += rowHeight + padding;  // After second dropdown (texture select)
    
    // Add extra rows for non-Albedo types
    if (node->textureType != TextureType::Albedo) {
        currentY += rowHeight + padding;  // After invert checkbox
        currentY += rowHeight + padding;  // After strength slider
    }
    
    float contentWidth = contentSize.x - padding * 2;
    
    // Get current texture thumbnail
    VkDescriptorSet currentThumbnail = VK_NULL_HANDLE;
    if (node->selectedTextureIndex >= 0 && node->selectedTextureIndex < static_cast<int>(m_availableTextures.size())) {
        currentThumbnail = m_availableTextures[node->selectedTextureIndex].thumbnail;
    }
    
    // === Preview Area ===
    float previewAreaHeight = contentSize.y - (currentY - contentPos.y) - padding;
    float previewAreaWidth = contentWidth;
    
    // Draw preview background
    ImVec2 previewPos = ImVec2(contentPos.x + padding, currentY);
    ImVec2 previewEnd = ImVec2(previewPos.x + previewAreaWidth, previewPos.y + previewAreaHeight);
    
    drawList->AddRectFilled(previewPos, previewEnd, IM_COL32(40, 40, 40, 255), 4.0f * m_viewZoom);
    drawList->AddRect(previewPos, previewEnd, IM_COL32(60, 60, 60, 255), 4.0f * m_viewZoom);
    
    // Draw texture preview if selected
    if (currentThumbnail != VK_NULL_HANDLE && previewAreaHeight > padding * 2) {
        // Calculate centered preview size maintaining aspect ratio
        float maxPreviewSize = std::min(previewAreaWidth, previewAreaHeight) - padding * 2;
        if (maxPreviewSize > 0) {
            ImVec2 imagePos = ImVec2(
                previewPos.x + (previewAreaWidth - maxPreviewSize) * 0.5f,
                previewPos.y + (previewAreaHeight - maxPreviewSize) * 0.5f
            );
            
            ImGui::SetCursorScreenPos(imagePos);
            ImGui::Image((ImTextureID)currentThumbnail, ImVec2(maxPreviewSize, maxPreviewSize));
        }
    } else if (previewAreaHeight > padding * 2) {
        // Draw "No Texture" text centered
        const char* noTexText = "No Texture";
        ImVec2 textSize = ImGui::CalcTextSize(noTexText);
        ImVec2 textPos = ImVec2(
            previewPos.x + (previewAreaWidth - textSize.x) * 0.5f,
            previewPos.y + (previewAreaHeight - textSize.y) * 0.5f
        );
        drawList->AddText(textPos, IM_COL32(120, 120, 120, 255), noTexText);
    }
}

} // namespace Forge
