#include "Engine.h"
#include "ui/ImGuiTheme.h"
#include "nodegraph/NodeGraph.h"
#include "project/ObjectFile.h"
#include "project/MaterialSettingsFile.h"
#include "scripting/PlayMode.h"
#include "platform/FileDialogs.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <array>
#include <glm/gtc/matrix_transform.hpp>

namespace Propulsion {

void Engine::renderAssetEditorTab(Tab& tab) {
    // Input Actions and Input Mappings are tables, not things to look at: their editor is one full-width panel.
    if (tab.contentItemIndex >= 0 && tab.contentItemIndex < static_cast<int>(m_contentItems.size())) {
        const std::string& assetType = m_contentItems[tab.contentItemIndex].type;
        if (assetType == "InputAction" || assetType == "InputMapping") {
            renderInputAssetEditorTab(tab);
            return;
        }
    }

    // Calculate the screen areas for left and right panels
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float menuBarHeight = ImGui::GetFrameHeight();
    float totalTopHeight = menuBarHeight + m_tabBarHeight;
    float footerHeight = ImGui::GetFrameHeight();  // Footer at bottom of window
    
    float totalWidth = viewport->Size.x;
    float contentHeight = viewport->Size.y - totalTopHeight - footerHeight;
    
    // Splitter configuration
    const float splitterWidth = PropulsionMKI::UI::scaled(6.0f);
    const float minPanelWidth = PropulsionMKI::UI::scaled(200.0f);
    
    // Get the splitter ratio from the content item (or use tab's copy)
    float splitterRatio = tab.splitterRatio;
    
    // Calculate panel widths based on splitter ratio
    float leftWidth = totalWidth * splitterRatio - splitterWidth * 0.5f;
    float rightWidth = totalWidth * (1.0f - splitterRatio) - splitterWidth * 0.5f;
    
    // Clamp to minimum widths
    leftWidth = (std::max)(leftWidth, minPanelWidth);
    rightWidth = (std::max)(rightWidth, minPanelWidth);
    
    // Left panel - Asset Preview Viewport
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + totalTopHeight));
    ImGui::SetNextWindowSize(ImVec2(leftWidth, contentHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar;
    
    // Use unique window IDs per tab to avoid conflicts
    std::string viewportId = "##AssetViewport" + std::to_string(tab.contentItemIndex);
    ImGui::Begin(viewportId.c_str(), nullptr, flags);
    
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    
    if (viewportSize.x > 0 && viewportSize.y > 0) {
        // Resize render target if viewport size changed
        uint32_t newWidth = static_cast<uint32_t>(viewportSize.x);
        uint32_t newHeight = static_cast<uint32_t>(viewportSize.y);
        VkExtent2D currentExtent = m_vulkanContext->getAssetPreviewExtent();
        
        if (newWidth != currentExtent.width || newHeight != currentExtent.height) {
            // Request deferred resize - will happen before next frame starts
            m_vulkanContext->requestAssetPreviewResize(newWidth, newHeight);
            tab.viewportWidth = static_cast<int>(newWidth);
            tab.viewportHeight = static_cast<int>(newHeight);
        }
        
        // Display the Vulkan asset preview texture at full viewport size
        ImTextureID texId = (ImTextureID)m_vulkanContext->getAssetPreviewDescriptorSet();
        ImGui::Image(texId, viewportSize);
        
        // Handle camera controls
        bool isHovered = ImGui::IsItemHovered();

        // An Object with a Camera component: outline what the camera sees, in the same orbit view as the preview.
        if (tab.contentItemIndex >= 0 && tab.contentItemIndex < static_cast<int>(m_contentItems.size())) {
            const ContentItem& shown = m_contentItems[tab.contentItemIndex];
            if (shown.type == "Object" && shown.camera.enabled) {
                glm::mat4 previewView, previewProjection;
                glm::vec3 previewEye;
                assetPreviewMatrices(tab, previewView, previewProjection, previewEye);
                SceneObject atOrigin("preview");  // the preview shows the object at the origin, in the same Z-up world as the level
                drawCameraFrustum(shown.camera, cameraWorldMatrix(atOrigin, shown.camera),
                                  previewView, previewProjection, viewportPos, viewportSize);
            }
        }

        // Right mouse button drag to orbit
        if (isHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            tab.cameraYaw += delta.x * 0.5f;
            tab.cameraPitch -= delta.y * 0.5f;  // Inverted for natural feel
            
            // Clamp pitch to prevent flipping
            tab.cameraPitch = glm::clamp(tab.cameraPitch, -89.0f, 89.0f);
        }
        
        // Scroll wheel to zoom
        if (isHovered) {
            float scroll = ImGui::GetIO().MouseWheel;
            if (scroll != 0.0f) {
                tab.cameraDistance -= scroll * 0.5f;
                tab.cameraDistance = glm::clamp(tab.cameraDistance, 1.0f, 20.0f);
            }
        }
        
        // Check if this is a material asset to show mesh selector
        if (tab.contentItemIndex >= 0 && tab.contentItemIndex < static_cast<int>(m_contentItems.size())) {
            ContentItem& item = m_contentItems[tab.contentItemIndex];
            if (item.type == "Material") {
                // Draw mesh selector buttons at bottom-right of viewport
                float buttonSize = PropulsionMKI::UI::scaled(28.0f);
                float buttonSpacing = PropulsionMKI::UI::scaled(4.0f);
                float padding = 8.0f;
                float totalButtonWidth = (buttonSize * 4) + (buttonSpacing * 3);
                
                ImVec2 buttonAreaPos = ImVec2(
                    viewportPos.x + viewportSize.x - totalButtonWidth - padding,
                    viewportPos.y + viewportSize.y - buttonSize - padding
                );
                
                ImGui::SetCursorScreenPos(buttonAreaPos);
                
                // Style for mesh selector buttons
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(buttonSpacing, 0));
                
                ImVec4 normalBg = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);
                ImVec4 activeBg = ImVec4(0.3f, 0.5f, 0.8f, 0.9f);
                ImVec4 hoverBg = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);
                
                // Cube button
                bool isCube = (tab.previewMeshType == PreviewMeshType::Cube);
                ImGui::PushStyleColor(ImGuiCol_Button, isCube ? activeBg : normalBg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isCube ? activeBg : hoverBg);
                if (ImGui::Button("##CubeBtn", ImVec2(buttonSize, buttonSize))) {
                    tab.previewMeshType = PreviewMeshType::Cube;
                }
                if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Cube");
                // Draw cube icon manually (simple box)
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 btnMin = ImGui::GetItemRectMin();
                ImVec2 btnMax = ImGui::GetItemRectMax();
                ImVec2 center = ImVec2((btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f);
                float iconSize = PropulsionMKI::UI::scaled(8.0f);
                dl->AddRect(ImVec2(center.x - iconSize, center.y - iconSize), 
                           ImVec2(center.x + iconSize, center.y + iconSize), 
                           IM_COL32(255, 255, 255, 200), 2.0f, 0, 2.0f);
                // Add 3D effect lines
                dl->AddLine(ImVec2(center.x - iconSize, center.y - iconSize), 
                           ImVec2(center.x - iconSize + 4, center.y - iconSize - 4), 
                           IM_COL32(255, 255, 255, 200), 1.5f);
                dl->AddLine(ImVec2(center.x + iconSize, center.y - iconSize), 
                           ImVec2(center.x + iconSize + 4, center.y - iconSize - 4), 
                           IM_COL32(255, 255, 255, 200), 1.5f);
                dl->AddLine(ImVec2(center.x - iconSize + 4, center.y - iconSize - 4), 
                           ImVec2(center.x + iconSize + 4, center.y - iconSize - 4), 
                           IM_COL32(255, 255, 255, 200), 1.5f);
                ImGui::PopStyleColor(2);
                
                ImGui::SameLine();
                
                // Sphere button
                bool isSphere = (tab.previewMeshType == PreviewMeshType::Sphere);
                ImGui::PushStyleColor(ImGuiCol_Button, isSphere ? activeBg : normalBg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isSphere ? activeBg : hoverBg);
                if (ImGui::Button("##SphereBtn", ImVec2(buttonSize, buttonSize))) {
                    tab.previewMeshType = PreviewMeshType::Sphere;
                }
                if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Sphere");
                // Draw sphere icon (circle)
                btnMin = ImGui::GetItemRectMin();
                btnMax = ImGui::GetItemRectMax();
                center = ImVec2((btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f);
                dl->AddCircle(center, iconSize, IM_COL32(255, 255, 255, 200), 24, 2.0f);
                ImGui::PopStyleColor(2);
                
                ImGui::SameLine();
                
                // Cylinder button
                bool isCylinder = (tab.previewMeshType == PreviewMeshType::Cylinder);
                ImGui::PushStyleColor(ImGuiCol_Button, isCylinder ? activeBg : normalBg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isCylinder ? activeBg : hoverBg);
                if (ImGui::Button("##CylinderBtn", ImVec2(buttonSize, buttonSize))) {
                    tab.previewMeshType = PreviewMeshType::Cylinder;
                }
                if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Cylinder");
                // Draw cylinder icon (rectangle with ellipses)
                btnMin = ImGui::GetItemRectMin();
                btnMax = ImGui::GetItemRectMax();
                center = ImVec2((btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f);
                float cylW = 7.0f, cylH = 10.0f;
                // Side lines
                dl->AddLine(ImVec2(center.x - cylW, center.y - cylH + 3), 
                           ImVec2(center.x - cylW, center.y + cylH - 3), 
                           IM_COL32(255, 255, 255, 200), 2.0f);
                dl->AddLine(ImVec2(center.x + cylW, center.y - cylH + 3), 
                           ImVec2(center.x + cylW, center.y + cylH - 3), 
                           IM_COL32(255, 255, 255, 200), 2.0f);
                // Top ellipse
                dl->AddEllipse(ImVec2(center.x, center.y - cylH + 3), ImVec2(cylW, 3), 
                              IM_COL32(255, 255, 255, 200), 0.0f, 16, 2.0f);
                // Bottom ellipse (half)
                dl->AddBezierQuadratic(
                    ImVec2(center.x - cylW, center.y + cylH - 3),
                    ImVec2(center.x, center.y + cylH + 1),
                    ImVec2(center.x + cylW, center.y + cylH - 3),
                    IM_COL32(255, 255, 255, 200), 2.0f, 16);
                ImGui::PopStyleColor(2);
                
                ImGui::PopStyleVar(2);
            }
        }
    }
    
    ImGui::End();
    
    // Splitter bar (invisible draggable area)
    float splitterX = viewport->Pos.x + leftWidth;
    ImGui::SetNextWindowPos(ImVec2(splitterX, viewport->Pos.y + totalTopHeight));
    ImGui::SetNextWindowSize(ImVec2(splitterWidth, contentHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags splitterFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;
    
    std::string splitterId = "##Splitter" + std::to_string(tab.contentItemIndex);
    ImGui::Begin(splitterId.c_str(), nullptr, splitterFlags);
    
    // Draw splitter visual
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 splitterMin = ImVec2(splitterX, viewport->Pos.y + totalTopHeight);
    ImVec2 splitterMax = ImVec2(splitterX + splitterWidth, viewport->Pos.y + totalTopHeight + contentHeight);
    
    // Check if mouse is hovering or dragging the splitter
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool isHoveringSplitter = mousePos.x >= splitterMin.x && mousePos.x <= splitterMax.x &&
                               mousePos.y >= splitterMin.y && mousePos.y <= splitterMax.y;
    
    static bool isDraggingSplitter = false;
    
    if (isHoveringSplitter || isDraggingSplitter) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        drawList->AddRectFilled(splitterMin, splitterMax, IM_COL32(100, 100, 100, 200));
    } else {
        drawList->AddRectFilled(splitterMin, splitterMax, IM_COL32(60, 60, 60, 150));
    }
    
    // Draw a subtle line in the middle
    float centerX = splitterX + splitterWidth * 0.5f;
    drawList->AddLine(ImVec2(centerX, splitterMin.y + 20), ImVec2(centerX, splitterMax.y - 20), 
                      IM_COL32(120, 120, 120, 200), 2.0f);
    
    // Handle splitter dragging
    if (isHoveringSplitter && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        isDraggingSplitter = true;
    }
    
    if (isDraggingSplitter) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float newSplitterX = mousePos.x - viewport->Pos.x;
            float newRatio = newSplitterX / totalWidth;
            
            // Clamp ratio to ensure minimum panel widths
            float minRatio = minPanelWidth / totalWidth;
            float maxRatio = 1.0f - minPanelWidth / totalWidth;
            newRatio = std::clamp(newRatio, minRatio, maxRatio);
            
            if (newRatio != tab.splitterRatio) {
                tab.splitterRatio = newRatio;
                
                // Update content item and mark as dirty
                if (tab.contentItemIndex >= 0 && tab.contentItemIndex < static_cast<int>(m_contentItems.size())) {
                    m_contentItems[tab.contentItemIndex].splitterRatio = newRatio;
                    tab.hasUnsavedChanges = true;
                }
            }
        } else {
            isDraggingSplitter = false;
        }
    }
    
    ImGui::End();
    
    // Right panel - Asset Properties
    ImGui::SetNextWindowPos(ImVec2(splitterX + splitterWidth, viewport->Pos.y + totalTopHeight));
    ImGui::SetNextWindowSize(ImVec2(rightWidth, contentHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    
    std::string propertiesId = "##AssetProperties" + std::to_string(tab.contentItemIndex);
    ImGui::Begin(propertiesId.c_str(), nullptr, flags);
    
    if (tab.contentItemIndex >= 0 && tab.contentItemIndex < static_cast<int>(m_contentItems.size())) {
        ContentItem& item = m_contentItems[tab.contentItemIndex];
        
        // Show different properties based on asset type
        if (item.type == "Material") {
            // Create node graph if it doesn't exist
            if (!tab.nodeGraph) {
                tab.nodeGraph = std::make_shared<NodeGraph>();
                
                // Initialize splitter ratio from content item
                tab.splitterRatio = item.splitterRatio;
                
                // Check if we have version 3 node data (full nodes + connections)
                if (!item.nodes.empty()) {
                    // Restore all nodes from saved data
                    for (const auto& nodeData : item.nodes) {
                        if (nodeData.nodeType == "MaterialOutput") {
                            auto outputNode = std::make_unique<MaterialOutputNode>(
                                nodeData.nodeId, 
                                ImVec2(nodeData.posX, nodeData.posY)
                            );
                            tab.nodeGraph->addNode(std::move(outputNode));
                        } else if (nodeData.nodeType == "ColorWheel") {
                            auto colorNode = std::make_unique<ColorWheelNode>(
                                nodeData.nodeId,
                                ImVec2(nodeData.posX, nodeData.posY)
                            );
                            // Restore color wheel specific data
                            colorNode->color = nodeData.color;
                            colorNode->hue = nodeData.hue;
                            colorNode->saturation = nodeData.saturation;
                            colorNode->value = nodeData.value;
                            colorNode->updateHexFromColor();
                            tab.nodeGraph->addNode(std::move(colorNode));
                        } else if (nodeData.nodeType == "Image") {
                            auto imageNode = std::make_unique<ImageNode>(
                                nodeData.nodeId,
                                ImVec2(nodeData.posX, nodeData.posY)
                            );
                            // Restore image node specific data
                            imageNode->selectedTexturePath = nodeData.texturePath;
                            imageNode->textureType = static_cast<TextureType>(nodeData.textureType);
                            imageNode->strength = nodeData.strength;
                            imageNode->invertTexture = nodeData.invertTexture;
                            imageNode->updateOutputPinType();
                            // Find the texture index from the path
                            imageNode->selectedTextureIndex = -1;
                            for (size_t ti = 0; ti < m_contentItems.size(); ti++) {
                                if (m_contentItems[ti].type == "Texture" && 
                                    m_contentItems[ti].filepath == nodeData.texturePath) {
                                    imageNode->selectedTextureIndex = static_cast<int>(ti);
                                    break;
                                }
                            }
                            tab.nodeGraph->addNode(std::move(imageNode));
                            CrashHandler::logInfo("Restored Image node with texture: " + nodeData.texturePath);
                        }
                    }
                    
                    // Restore all connections
                    for (const auto& conn : item.connections) {
                        tab.nodeGraph->addConnection(
                            conn.outputNodeId,
                            conn.outputPinId,
                            conn.inputNodeId,
                            conn.inputPinId
                        );
                    }
                } else if (!item.nodePositions.empty()) {
                    // Legacy version 2: only node positions, create default output node
                    ImVec2 outputNodePos(10, 2);  // Default position
                    for (const auto& nodePos : item.nodePositions) {
                        if (nodePos.nodeType == "MaterialOutput") {
                            outputNodePos = ImVec2(nodePos.posX, nodePos.posY);
                            break;
                        }
                    }
                    auto outputNode = std::make_unique<MaterialOutputNode>(0, outputNodePos);
                    tab.nodeGraph->addNode(std::move(outputNode));
                } else {
                    // No saved data, create default output node
                    auto outputNode = std::make_unique<MaterialOutputNode>(0, ImVec2(10, 2));
                    tab.nodeGraph->addNode(std::move(outputNode));
                }
            }
            
            // How the surface lets light through (solid, cut out, see-through)
            if (drawMaterialSurfaceSettings(item)) {
                tab.hasUnsavedChanges = true;
                m_unsavedContentItems.insert(tab.contentItemIndex);
                m_projectModified = true;
            }
            ImGui::Separator();

            // Get available size for the graph
            ImVec2 graphSize = ImGui::GetContentRegionAvail();
            
            // Update available textures for Image nodes
            std::vector<TextureInfo> textures;
            for (const auto& contentItem : m_contentItems) {
                if (contentItem.type == "Texture") {
                    textures.emplace_back(contentItem.name, contentItem.filepath, contentItem.thumbnailTexture);
                }
            }
            tab.nodeGraph->setAvailableTextures(textures);
            
            // Render the node graph editor
            tab.nodeGraph->render(graphSize);
            
            // Sync node graph unsaved changes to tab and project
            if (tab.nodeGraph->hasUnsavedChanges()) {
                tab.hasUnsavedChanges = true;
                // Add to project-level unsaved tracking
                m_unsavedContentItems.insert(tab.contentItemIndex);
                m_projectModified = true;
            }
            
            // Handle Ctrl+S to save (check for key press globally in this window context)
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
                // Save material file to disk
                if (!m_currentProjectPath.empty() && !item.filepath.empty()) {
                    try {
                        CrashHandler::logInfo("Saving material (Ctrl+S): " + item.name);
                        std::filesystem::path projectFile(m_currentProjectPath);
                        std::filesystem::path projectDir = projectFile.parent_path();
                        std::filesystem::path materialFile = projectDir / item.filepath;
                        
                        // Create parent directories if they don't exist
                        std::filesystem::create_directories(materialFile.parent_path());
                        
                        // Update nodes in ContentItem from node graph (full data for v4)
                        item.nodes.clear();
                        for (const auto& node : tab.nodeGraph->getNodes()) {
                            MaterialNodeData nodeData;
                            nodeData.nodeType = node->type;
                            nodeData.nodeId = node->id;
                            nodeData.posX = node->position.x;
                            nodeData.posY = node->position.y;
                            
                            // Save ColorWheel-specific properties
                            if (node->type == "ColorWheel") {
                                auto* colorNode = dynamic_cast<ColorWheelNode*>(node.get());
                                if (colorNode) {
                                    nodeData.color = colorNode->color;
                                    nodeData.hue = colorNode->hue;
                                    nodeData.saturation = colorNode->saturation;
                                    nodeData.value = colorNode->value;
                                }
                            }
                            // Save Image node-specific properties
                            else if (node->type == "Image") {
                                auto* imageNode = dynamic_cast<ImageNode*>(node.get());
                                if (imageNode) {
                                    nodeData.texturePath = imageNode->selectedTexturePath;
                                    nodeData.textureType = static_cast<int>(imageNode->textureType);
                                    nodeData.strength = imageNode->strength;
                                    nodeData.invertTexture = imageNode->invertTexture;
                                }
                            }
                            item.nodes.push_back(nodeData);
                        }
                        
                        // Update connections from node graph
                        item.connections.clear();
                        for (const auto& conn : tab.nodeGraph->getConnections()) {
                            MaterialConnection connData;
                            connData.outputNodeId = conn.outputNodeId;
                            connData.outputPinId = conn.outputPinId;
                            connData.inputNodeId = conn.inputNodeId;
                            connData.inputPinId = conn.inputPinId;
                            item.connections.push_back(connData);
                        }
                        
                        // Evaluate the node graph to get the actual material properties FIRST
                        EvaluatedMaterial evalMat = tab.nodeGraph->evaluateMaterial();
                        item.materialColor = evalMat.baseColor;  // Update stored color
                        
                        // Regenerate material thumbnail with evaluated material (supports textures)
                        // This populates item.thumbnailPixels for saving
                        generateMaterialThumbnailWithTexture(item, evalMat.baseColor, 
                            evalMat.useAlbedoTexture, evalMat.albedoTexturePath);
                        CrashHandler::logInfo("Material thumbnail regenerated for: " + item.name);
                        
                        std::ofstream matFile(materialFile, std::ios::binary);
                        if (matFile.is_open()) {
                            // Write header with version for forward compatibility
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
                                // Node id
                                matFile.write(reinterpret_cast<const char*>(&nodeData.nodeId), sizeof(int));
                                // Position
                                matFile.write(reinterpret_cast<const char*>(&nodeData.posX), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.posY), sizeof(float));
                                // ColorWheel properties (write for all nodes, only meaningful for ColorWheel)
                                matFile.write(reinterpret_cast<const char*>(&nodeData.color.r), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.color.g), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.color.b), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.hue), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.saturation), sizeof(float));
                                matFile.write(reinterpret_cast<const char*>(&nodeData.value), sizeof(float));
                                // Image node properties
                                size_t pathLength = nodeData.texturePath.length();
                                matFile.write(reinterpret_cast<const char*>(&pathLength), sizeof(size_t));
                                if (pathLength > 0) {
                                    matFile.write(nodeData.texturePath.c_str(), pathLength);
                                }
                                matFile.write(reinterpret_cast<const char*>(&nodeData.textureType), sizeof(int));
                                // Texture strength
                                matFile.write(reinterpret_cast<const char*>(&nodeData.strength), sizeof(float));
                                // Invert texture flag
                                uint8_t invertFlag = nodeData.invertTexture ? 1 : 0;
                                matFile.write(reinterpret_cast<const char*>(&invertFlag), sizeof(uint8_t));
                            }
                            
                            // Write connections (v3)
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
                            MaterialSettingsFile::write(matFile, item.materialSettings);
                            
                            matFile.close();
                            CrashHandler::logInfo("Material saved (v7) with " + std::to_string(nodeCount) + " nodes, " + 
                                std::to_string(connCount) + " connections, and thumbnail (" + 
                                std::to_string(item.thumbnailWidth) + "x" + std::to_string(item.thumbnailHeight) + ")");
                            CrashHandler::logInfo("Material saved successfully: " + materialFile.string());
                            
                            // Update all scene objects that use this material
                            int materialIndex = tab.contentItemIndex;
                            for (auto& obj : m_sceneObjects) {
                                if (objectUsesMaterial(obj, materialIndex)) {
                                    obj.color = evalMat.baseColor;
                                    CrashHandler::logInfo("Updated scene object '" + obj.name + "' with new material color");
                                }
                            }
                        } else {
                            CrashHandler::logError("Failed to open material file for writing: " + materialFile.string());
                        }
                    } catch (const std::exception& e) {
                        CrashHandler::logError("Exception saving material: " + std::string(e.what()));
                    }
                }
                
                tab.hasUnsavedChanges = false;
                if (tab.nodeGraph) {
                    tab.nodeGraph->clearUnsavedChanges();
                }
                // Remove from project-level unsaved tracking
                m_unsavedContentItems.erase(tab.contentItemIndex);
                // Update project modified status
                m_projectModified = !m_unsavedContentItems.empty();
                
                OutputLog::info(LogCategory::Content, "Saved material: " + item.name);
            }
        } else if (item.type == "Object") {
            ImGui::Text("Asset Properties");
            ImGui::Separator();
            ImGui::Text("Object Editor");
            ImGui::Separator();

            // The same property widgets the Graph view shows (see ObjectProperties.cpp).
            bool changed = false;
            changed |= drawObjectMeshProperty(item);
            ImGui::Spacing();
            changed |= drawObjectMaterialProperty(item);
            ImGui::Spacing();
            changed |= drawObjectScriptProperty(item, false);
            ImGui::Spacing();
            changed |= drawObjectInputProperty(item);
            ImGui::Spacing();
            ImGui::Separator();
            changed |= drawObjectComponents(item);
            if (changed) tab.hasUnsavedChanges = true;

            ImGui::Spacing();
            ImGui::Separator();

            // Show save status
            if (tab.hasUnsavedChanges) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "* Unsaved changes");
                ImGui::Text("Press Ctrl+S to save");
            }

            // Handle Ctrl+S to save
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {

                saveObjectAsset(item);
                tab.hasUnsavedChanges = false;
                OutputLog::info(LogCategory::Content, "Saved object: " + item.name);
            }
        } else {
            ImGui::Text("Mesh Properties");
            ImGui::Separator();
            ImGui::Text("(Mesh properties will be added here)");
        }
    } else {
        ImGui::Text("Asset Properties");
        ImGui::Separator();
        ImGui::Text("(No asset selected)");
    }
    
    ImGui::End();
}

void Engine::renderAssetPreview(Tab& tab, VkCommandBuffer commandBuffer) {
    // Find the mesh to render based on the content item
    Mesh* meshToRender = nullptr;
    glm::vec3 meshColor = glm::vec3(0.7f, 0.7f, 0.7f);
    EvaluatedMaterial evalMat; // Full material evaluation
    MaterialSettings materialSettings;       // a Material asset: solid, cut out or see-through
    bool drawAsObject = false;               // an Object asset: use objectPreviewInfo, and show its camera
    MaterialRenderInfo objectPreviewInfo;
    CameraSettings objectCamera;
    LightSettings objectLight;               // an Object asset with a Light component shows a picture of it
    
    if (tab.contentItemIndex >= 0 && tab.contentItemIndex < static_cast<int>(m_contentItems.size())) {
        ContentItem& item = m_contentItems[tab.contentItemIndex];
        
        if (item.type == "Mesh") {
            // For mesh assets, render the mesh itself
            if (item.name == "Cube") {
                meshToRender = m_cube.get();
            } else if (item.name == "Sphere") {
                meshToRender = m_sphere.get();
            } else if (item.name == "Cylinder") {
                meshToRender = m_cylinder.get();
            } else {
                meshToRender = m_cube.get(); // Default
            }
        } else if (item.type == "Object") {
            // Draw the object the way the level does: its mesh, and its material with textures.
            objectPreviewInfo = getMaterialRenderInfo(proxyObjectOf(item));
            drawAsObject = true;
            objectCamera = item.camera;
            objectLight = item.light;

            // For object assets, render the referenced mesh with material color
            if (!item.meshReferenceGUID.empty()) {
                meshToRender = meshByGUID(item.meshReferenceGUID);
            } else if (item.meshReference >= 0 && item.meshReference < static_cast<int>(m_contentItems.size())) {
                const auto& meshItem = m_contentItems[item.meshReference];
                if (meshItem.name == "Sphere") {
                    meshToRender = m_sphere.get();
                } else if (meshItem.name == "Cylinder") {
                    meshToRender = m_cylinder.get();
                } else {
                    meshToRender = m_cube.get();
                }
            } else {
                meshToRender = nullptr;  // no mesh assigned: the Object has no mesh to show
            }
            
            // Get material color if assigned
            if (item.materialReference >= 0 && item.materialReference < static_cast<int>(m_contentItems.size())) {
                meshColor = m_contentItems[item.materialReference].materialColor;
            }
        } else if (item.type == "Material") {
            // For material assets, render the selected preview mesh
            switch (tab.previewMeshType) {
                case PreviewMeshType::Cube:
                    meshToRender = m_cube.get();
                    break;
                case PreviewMeshType::Sphere:
                    meshToRender = m_sphere.get();
                    break;
                case PreviewMeshType::Cylinder:
                    meshToRender = m_cylinder.get();
                    break;
                default:
                    meshToRender = m_cube.get();
                    break;
            }
            
            // Evaluate the node graph for live color and texture updates
            materialSettings = item.materialSettings;
            if (tab.nodeGraph) {
                evalMat = tab.nodeGraph->evaluateMaterial();
                meshColor = evalMat.baseColor;
            } else {
                meshColor = item.materialColor;
            }
        }
    }
    
    if (!meshToRender && !drawAsObject) {
        meshToRender = m_cube.get(); // Fallback (an Object with no mesh is drawn without one)
    }
    
    // The preview camera orbits the object (Z-up world); the Object editor draws its frustum with the same matrices.
    glm::mat4 view, projection;
    glm::vec3 cameraPos;
    assetPreviewMatrices(tab, view, projection, cameraPos);
    VkExtent2D extent = m_vulkanContext->getAssetPreviewExtent();

    // No model rotation needed - FBX models are Z-up, our world is Z-up
    glm::mat4 model = glm::mat4(1.0f);
    
    // NOTE: Texture binding is now done BEFORE beginFrame() in Engine::render()
    // to avoid invalidating descriptor sets during command buffer recording
    
    // Begin asset preview render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_vulkanContext->getAssetPreviewRenderPass();
    renderPassInfo.framebuffer = m_vulkanContext->getAssetPreviewFramebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.15f, 0.15f, 0.18f, 1.0f}}; // Dark background
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    // Bind shader and draw mesh FIRST (opaque geometry). A see-through material is drawn blended, over the preview's background.
    const MaterialSettings& shownSettings = drawAsObject ? objectPreviewInfo.settings : materialSettings;
    const bool shownTranslucent = shownSettings.blendMode == BlendMode::Translucent;
    m_offscreenShader->bind(commandBuffer, shownTranslucent ? m_pipelineBlend : 0);
    m_offscreenShader->setCullMode(commandBuffer, shownSettings.twoSided);
    // The preview is lit by the default lights and the scene's sky, not by the lights in the level
    m_lighting->bind(commandBuffer, m_offscreenShader->getPipelineLayout(), 1, m_vulkanContext->getCurrentFrame(), SceneLighting::Slot::Preview);
    if (m_textureTable) {
        const VkDescriptorSet table = m_textureTable->getSet(m_vulkanContext->getCurrentFrame());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_offscreenShader->getPipelineLayout(), 2, 1, &table, 0, nullptr);
    }

    // The same uniform block the level uses (its textures are already in the texture table: Engine::render loaded them before recording)
    UniformBufferObject ubo;
    if (drawAsObject) {
        ubo = makeMaterialUbo(objectPreviewInfo, model, view, projection, cameraPos);
    } else {
        MaterialRenderInfo previewInfo = materialRenderInfoFrom(evalMat, materialSettings);
        previewInfo.baseColor = meshColor;
        ubo = makeMaterialUbo(previewInfo, model, view, projection, cameraPos);
    }

    // Each thing drawn gets its own block of the uniform buffer (a single block would be overwritten by the
    // next draw before the GPU reads it).
    const uint32_t frame = m_vulkanContext->getCurrentFrame();
    m_offscreenShader->resetObjectIndex();
    m_offscreenShader->updateUniformBufferDynamic(frame, ubo);
    m_offscreenShader->bindWithDynamicOffset(commandBuffer, frame, 0);
    if (meshToRender) meshToRender->draw(commandBuffer);

    // An Object with a Camera component shows the camera too, where the level would put it (the preview is the
    // same Z-up world, with the object at the origin).
    if (drawAsObject && objectCamera.enabled && m_cameraMesh) {
        SceneObject atOrigin("preview");
        MaterialRenderInfo cameraLook;
        cameraLook.baseColor = glm::vec3(0.35f, 0.65f, 1.0f);
        cameraLook.roughness = 0.6f;
        UniformBufferObject cameraUbo = makeMaterialUbo(cameraLook, cameraWorldMatrix(atOrigin, objectCamera) * CameraSettings::meshCorrection(),
                                                        view, projection, cameraPos);
        m_offscreenShader->updateUniformBufferDynamic(frame, cameraUbo);
        m_offscreenShader->bindWithDynamicOffset(commandBuffer, frame, 1);
        m_cameraMesh->draw(commandBuffer);
    }

    // ... and a light shows where it is: at the origin, the sun aimed the way a new one is when it is placed in the level.
    if (drawAsObject && objectLight.enabled) {
        const uint32_t firstFree = (objectCamera.enabled && m_cameraMesh) ? 2 : 1;  // the mesh is block 0, the camera block 1
        uint32_t drawIndex = firstFree;
        const glm::mat4 place = glm::mat4_cast(objectLight.type == LightSettings::Type::Directional ? rotationFromForwardTo(glm::vec3(-0.4f, 0.5f, -1.0f))
                                                                                                    : glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        drawLightIcon(commandBuffer, objectLight, place, view, projection, cameraPos, frame, drawIndex);
    }
    
    // Render world grid AFTER mesh (transparent, with depth testing to properly occlude/be occluded)
    m_worldGrid->render(commandBuffer, view, projection, cameraPos);
    
    vkCmdEndRenderPass(commandBuffer);
}

} // namespace Propulsion
