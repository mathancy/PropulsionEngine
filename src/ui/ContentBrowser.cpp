#include "Engine.h"
#include "ui/ImGuiTheme.h"
#include "rendering/MeshLoader.h"
#include "project/ObjectFile.h"
#include "input/InputTypes.h"
#include "utils/Utils.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include "ui/IconsFontAwesome7.h"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <stb_image.h>

#ifdef _WIN32
#undef max  // Prevent Windows.h max macro from conflicting with std::max
#endif

#include "platform/FileDialogs.h"

namespace Propulsion {

void Engine::renderContentBrowser() {
    ImGui::Begin("Content Browser");
    
    // Split into hierarchy (left) and assets (right)
    float hierarchyWidth = PropulsionMKI::UI::scaled(150.0f);
    
    // Left side: Folder Hierarchy
    ImGui::BeginChild("Hierarchy", ImVec2(hierarchyWidth, 0), true);
    
    // Header with Folders title and refresh button (compact layout)
    float buttonSize = ImGui::GetFrameHeight() * 0.6f;
    float padding = PropulsionMKI::UI::scaled(5.0f);

    // Set initial cursor position for the row
    ImGui::SetCursorPosY(padding);
    
    // Draw title aligned with button center
    float textHeight = ImGui::GetTextLineHeight();
    float textOffsetY = (buttonSize - textHeight) * 0.5f; // Center text vertically with button
    ImGui::SetCursorPosY(padding + textOffsetY);
    ImGui::Text("Folders");
    
    // Draw button on same row
    ImGui::SameLine();
    ImGui::SetCursorPosX(hierarchyWidth - buttonSize - padding);
    ImGui::SetCursorPosY(padding); // Button at top with padding
    
    ImGui::PushFont(m_fontRegular); // Use regular font which has Font Awesome merged
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(PropulsionMKI::UI::scaled(2), PropulsionMKI::UI::scaled(2))); // Smaller padding for tighter icon fit
    ImGui::SetWindowFontScale(0.7f);
    if (ImGui::Button(ICON_FA_ROTATE, ImVec2(buttonSize, buttonSize))) {
        scanAssetsDirectory(true); // Rescan ALL folders to update tab names
        CrashHandler::logInfo("Manual refresh triggered - rescanning ALL assets");
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    
    if (ImGui::IsItemHovered()) {
        PropulsionMKI::UI::tooltip("Refresh all assets");
    }
    
    // Move cursor down to just below button with padding
    ImGui::SetCursorPosY(padding + buttonSize + padding);
    ImGui::Separator();
    
    for (size_t i = 0; i < m_folders.size(); i++) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (m_selectedFolderIndex == static_cast<int>(i)) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        
        ImGui::TreeNodeEx((void*)(intptr_t)i, flags, "%s", m_folders[i].c_str());
        
        if (ImGui::IsItemClicked()) {
            m_selectedFolderIndex = static_cast<int>(i);
            m_currentFolder = m_folders[i];
            scanAssetsDirectory(true); // Rescan ALL folders to keep meshes/materials available
        }
        
        // Right-click context menu for folder
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Rename")) {
                m_showRenamePopup = true;
                m_renamingFolder = true;
                m_renamingFolderIndex = static_cast<int>(i);
                m_renamingItemIndex = -1;
                strncpy_s(m_renameBuffer, m_folders[i].c_str(), sizeof(m_renameBuffer) - 1);
            }
            ImGui::EndPopup();
        }
        
        // F2 to rename selected folder
        if (m_selectedFolderIndex == static_cast<int>(i) && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2)) {
            m_showRenamePopup = true;
            m_renamingFolder = true;
            m_renamingFolderIndex = static_cast<int>(i);
            m_renamingItemIndex = -1;
            strncpy_s(m_renameBuffer, m_folders[i].c_str(), sizeof(m_renameBuffer) - 1);
        }
    }
    
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // Right side: Assets in current folder
    ImGui::BeginChild("Assets", ImVec2(0, 0), true);
    
    ImGui::Text("Project Assets - %s", m_currentFolder.c_str());
    ImGui::Separator();
    
    // Grid layout for content items
    float cellSize = PropulsionMKI::UI::scaled(100.0f);
    float cellPadding = PropulsionMKI::UI::scaled(20.0f);  // Increased padding to prevent cutoff
    float borderSize = PropulsionMKI::UI::scaled(2.0f);
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = std::max(1, static_cast<int>(panelWidth / (cellSize + cellPadding + borderSize * 4)));
    
    ImGui::Columns(columnCount, nullptr, false);
    
    for (size_t i = 0; i < m_contentItems.size(); i++) {
        ContentItem& item = m_contentItems[i];
        
        // Skip items not in current folder
        if (item.folder != m_currentFolder) {
            continue;
        }
        
        ImGui::PushID(static_cast<int>(i));
        
        // Thumbnail
        ImVec4 borderColor = (m_selectedContentIndex == static_cast<int>(i)) 
            ? ImVec4(1.0f, 0.42f, 0.00f, 1.0f)  // Orange for selected
            : ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.25f, 0.20f, 1.0f)); // Subtle orange tint
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.30f, 0.20f, 1.0f));  // Stronger orange tint
        ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        
        if (item.thumbnailTexture != VK_NULL_HANDLE) {
            // Use Vulkan descriptor set directly with ImGui
            if (ImGui::ImageButton("##thumbnail", (ImTextureID)item.thumbnailTexture, 
                ImVec2(cellSize, cellSize), ImVec2(0, 0), ImVec2(1, 1))) {
                m_selectedContentIndex = static_cast<int>(i);
            }
        } else {
            if (ImGui::Button("##thumbnail", ImVec2(cellSize, cellSize))) {
                m_selectedContentIndex = static_cast<int>(i);
            }
        }
        
        // Input assets have no picture: say what they are on the button
        if (item.type == "InputAction" || item.type == "InputMapping") {
            const char* label = item.type == "InputAction" ? (item.inputAction.type == InputValueType::Axis ? "AXIS" : "BUTTON") : "KEYS";
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax(), ts = ImGui::CalcTextSize(label);
            ImGui::GetWindowDrawList()->AddText(ImVec2((mn.x + mx.x - ts.x) * 0.5f, (mn.y + mx.y - ts.y) * 0.5f), IM_COL32(230, 230, 230, 255), label);
        }

        // Double-click an HDRI to make it the scene's sky
        if (item.type == "HDRI" && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            useHdriAsEnvironment(item);
        }

        // Double-click to open asset editor tab
        if (item.type != "HDRI" && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            // Check if tab already exists for this asset
            bool tabExists = false;
            for (size_t t = 0; t < m_tabs.size(); t++) {
                if (m_tabs[t].type == TabType::AssetEditor && !item.id.empty() && m_tabs[t].assetId == item.id) {
                    m_activeTabIndex = static_cast<int>(t);
                    m_tabs[t].needsSelection = true;  // Force tab selection
                    tabExists = true;
                    break;
                }
            }
            
            // Create new tab if doesn't exist
            if (!tabExists) {
                Tab newTab(item.name, TabType::AssetEditor, static_cast<int>(i));
                newTab.assetId = item.id;
                m_tabs.push_back(newTab);
                m_activeTabIndex = static_cast<int>(m_tabs.size() - 1);
            }
        }
        
        // Drag and drop source
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("CONTENT_ITEM", &i, sizeof(size_t));
            ImGui::Text("%s", item.name.c_str());
            ImGui::EndDragDropSource();
        }
        
        // Right-click context menu for asset
        if (ImGui::BeginPopupContextItem()) {
            if (item.type == "HDRI" && ImGui::MenuItem("Use as Environment")) {
                useHdriAsEnvironment(item);
            }
            if (ImGui::MenuItem("Rename")) {
                m_showRenamePopup = true;
                m_renamingFolder = false;
                m_renamingItemIndex = static_cast<int>(i);
                m_renamingFolderIndex = -1;
                strncpy_s(m_renameBuffer, item.name.c_str(), sizeof(m_renameBuffer) - 1);
            }
            ImGui::EndPopup();
        }
        
        // F2 to rename selected asset
        if (m_selectedContentIndex == static_cast<int>(i) && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2)) {
            m_showRenamePopup = true;
            m_renamingFolder = false;
            m_renamingItemIndex = static_cast<int>(i);
            m_renamingFolderIndex = -1;
            strncpy_s(m_renameBuffer, item.name.c_str(), sizeof(m_renameBuffer) - 1);
        }
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        
        // Get the column offset and width for proper centering
        float columnOffset = ImGui::GetColumnOffset();
        float columnWidth = ImGui::GetColumnWidth();
        
        // Item name - centered and truncated if too long
        std::string displayName = item.name;
        float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
        float availableWidth = cellSize;
        bool isTruncated = false;
        
        // Truncate with ellipsis if name is too long
        if (textWidth > availableWidth) {
            isTruncated = true;
            const char* ellipsis = "...";
            float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
            
            // Binary search for the right truncation point
            int charCount = static_cast<int>(displayName.length());
            while (charCount > 0) {
                std::string truncated = displayName.substr(0, charCount) + ellipsis;
                if (ImGui::CalcTextSize(truncated.c_str()).x <= availableWidth) {
                    displayName = truncated;
                    break;
                }
                charCount--;
            }
            textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
        }
        
        // Center the text within the column using absolute position
        float textPosX = columnOffset + (columnWidth - textWidth) * 0.5f;
        ImGui::SetCursorPosX(textPosX);
        ImGui::Text("%s", displayName.c_str());
        
        // Show tooltip with full name if truncated
        if (isTruncated && ImGui::IsItemHovered()) {
            PropulsionMKI::UI::tooltip("%s", item.name.c_str());
        }
        
        // Object type - centered
        float typeWidth = ImGui::CalcTextSize(item.type.c_str()).x;
        float typePosX = columnOffset + (columnWidth - typeWidth) * 0.5f;
        ImGui::SetCursorPosX(typePosX);
        ImGui::TextDisabled("%s", item.type.c_str());
        
        ImGui::PopID();
        
        ImGui::NextColumn();
    }
    
    ImGui::Columns(1);
    
    // Right-click context menu for creating new assets
    if (ImGui::BeginPopupContextWindow("ContentBrowserContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Material")) {
            m_showMaterialNamePopup = true;
            memset(m_newMaterialName, 0, sizeof(m_newMaterialName));
            strcpy_s(m_newMaterialName, sizeof(m_newMaterialName), "NewMaterial");
        }
        if (ImGui::MenuItem("Create Object")) {
            m_showObjectNamePopup = true;
            m_newObjectLightType = -1;
            memset(m_newObjectName, 0, sizeof(m_newObjectName));
            strcpy_s(m_newObjectName, sizeof(m_newObjectName), "NewObject");
        }
        if (ImGui::MenuItem("Create Directional Light")) {
            m_showObjectNamePopup = true;
            m_newObjectLightType = 0;
            memset(m_newObjectName, 0, sizeof(m_newObjectName));
            strcpy_s(m_newObjectName, sizeof(m_newObjectName), "Sun");
        }
        if (ImGui::MenuItem("Create Point Light")) {
            m_showObjectNamePopup = true;
            m_newObjectLightType = 1;
            memset(m_newObjectName, 0, sizeof(m_newObjectName));
            strcpy_s(m_newObjectName, sizeof(m_newObjectName), "PointLight");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Create Input Action")) {
            m_showInputActionNamePopup = true;
            memset(m_newInputActionName, 0, sizeof(m_newInputActionName));
            strcpy_s(m_newInputActionName, sizeof(m_newInputActionName), "newAction");
        }
        if (ImGui::MenuItem("Create Input Mapping")) {
            m_showInputMappingNamePopup = true;
            memset(m_newInputMappingName, 0, sizeof(m_newInputMappingName));
            strcpy_s(m_newInputMappingName, sizeof(m_newInputMappingName), "NewInputMapping");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import File...")) {
            m_showImportDialog = true;
        }
        ImGui::EndPopup();
    }
    
    // Handle import file dialog (triggered from context menu)
    if (m_showImportDialog) {
        m_showImportDialog = false;
        std::vector<std::string> importPaths = PropulsionMKI::Platform::showImportFileDialog(m_window->getNativeWindow());
        for (const auto& importPath : importPaths) {
            importFile(importPath);
        }
    }
    
    // Material name input popup
    if (m_showMaterialNamePopup) {
        ImGui::OpenPopup("Create Material");
    }
    
    if (ImGui::BeginPopupModal("Create Material", &m_showMaterialNamePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter material name:");
        ImGui::InputText("##MaterialName", m_newMaterialName, sizeof(m_newMaterialName));
        
        if (ImGui::Button("Create", ImVec2(PropulsionMKI::UI::scaled(120), 0))) {
            if (strlen(m_newMaterialName) > 0) {
                createMaterial(m_newMaterialName);
                m_showMaterialNamePopup = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(PropulsionMKI::UI::scaled(120), 0))) {
            m_showMaterialNamePopup = false;
        }
        
        ImGui::EndPopup();
    }
    
    // Object name input popup
    if (m_showObjectNamePopup) {
        ImGui::OpenPopup("Create Object");
    }
    
    if (ImGui::BeginPopupModal("Create Object", &m_showObjectNamePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter object name:");
        ImGui::InputText("##ObjectName", m_newObjectName, sizeof(m_newObjectName));
        
        if (ImGui::Button("Create", ImVec2(PropulsionMKI::UI::scaled(120), 0))) {
            if (strlen(m_newObjectName) > 0) {
                createObject(m_newObjectName, m_newObjectLightType);
                m_showObjectNamePopup = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(PropulsionMKI::UI::scaled(120), 0))) {
            m_showObjectNamePopup = false;
        }
        
        ImGui::EndPopup();
    }
    
    // Input Action and Input Mapping name popups
    if (m_showInputActionNamePopup) ImGui::OpenPopup("Create Input Action");
    if (ImGui::BeginPopupModal("Create Input Action", &m_showInputActionNamePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter the action's name (scripts use it):");
        ImGui::InputText("##InputActionName", m_newInputActionName, sizeof(m_newInputActionName));
        ImGui::TextDisabled("Letters, digits and _, starting with a letter: jump, moveForward, fire_primary");
        if (ImGui::Button("Create", ImVec2(PropulsionMKI::UI::scaled(120), 0))) {
            if (strlen(m_newInputActionName) > 0) {
                createInputAsset("InputAction", m_newInputActionName);
                m_showInputActionNamePopup = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(PropulsionMKI::UI::scaled(120), 0))) m_showInputActionNamePopup = false;
        ImGui::EndPopup();
    }
    if (m_showInputMappingNamePopup) ImGui::OpenPopup("Create Input Mapping");
    if (ImGui::BeginPopupModal("Create Input Mapping", &m_showInputMappingNamePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter mapping name:");
        ImGui::InputText("##InputMappingName", m_newInputMappingName, sizeof(m_newInputMappingName));
        if (ImGui::Button("Create", ImVec2(PropulsionMKI::UI::scaled(120), 0))) {
            if (strlen(m_newInputMappingName) > 0) {
                createInputAsset("InputMapping", m_newInputMappingName);
                m_showInputMappingNamePopup = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(PropulsionMKI::UI::scaled(120), 0))) m_showInputMappingNamePopup = false;
        ImGui::EndPopup();
    }

    // Rename popup
    if (m_showRenamePopup) {
        ImGui::OpenPopup("Rename");
    }
    
    if (ImGui::BeginPopupModal("Rename", &m_showRenamePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (m_renamingFolder) {
            ImGui::Text("Rename folder:");
        } else {
            ImGui::Text("Rename asset:");
        }
        
        ImGui::InputText("##RenameName", m_renameBuffer, sizeof(m_renameBuffer));
        
        if (ImGui::Button("Rename", ImVec2(PropulsionMKI::UI::scaled(120), 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            if (strlen(m_renameBuffer) > 0) {
                if (m_renamingFolder && m_renamingFolderIndex >= 0 && m_renamingFolderIndex < static_cast<int>(m_folders.size())) {
                    // Rename folder
                    std::string oldFolderName = m_folders[m_renamingFolderIndex];
                    std::string newFolderName = m_renameBuffer;
                    
                    if (!m_currentProjectPath.empty()) {
                        try {
                            std::filesystem::path projectDir(m_currentProjectPath);
                            std::filesystem::path oldFolderPath = projectDir / oldFolderName;
                            std::filesystem::path newFolderPath = projectDir / newFolderName;
                            
                            // Rename directory on disk
                            if (std::filesystem::exists(oldFolderPath)) {
                                std::filesystem::rename(oldFolderPath, newFolderPath);
                                CrashHandler::logInfo("Renamed folder: " + oldFolderName + " -> " + newFolderName);
                            }
                            
                            // Update folder name in list
                            m_folders[m_renamingFolderIndex] = newFolderName;
                            
                            // Update current folder if it was selected
                            if (m_currentFolder == oldFolderName) {
                                m_currentFolder = newFolderName;
                            }
                            
                            // Update all content items that belong to this folder
                            for (auto& item : m_contentItems) {
                                if (item.folder == oldFolderName) {
                                    item.folder = newFolderName;
                                    
                                    // Update filepath
                                    if (!item.filepath.empty()) {
                                        std::filesystem::path oldPath(item.filepath);
                                        std::filesystem::path newPath = newFolderName / oldPath.filename();
                                        item.filepath = newPath.string();
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            std::string error = "Failed to rename folder: " + std::string(e.what());
                            CrashHandler::logError(error);
                        }
                    } else {
                        // Just update the name if no project is saved
                        m_folders[m_renamingFolderIndex] = newFolderName;
                        if (m_currentFolder == oldFolderName) {
                            m_currentFolder = newFolderName;
                        }
                        for (auto& item : m_contentItems) {
                            if (item.folder == oldFolderName) {
                                item.folder = newFolderName;
                            }
                        }
                    }
                } else if (!m_renamingFolder && m_renamingItemIndex >= 0 && m_renamingItemIndex < static_cast<int>(m_contentItems.size())) {
                    // Rename asset
                    ContentItem& item = m_contentItems[m_renamingItemIndex];
                    std::string oldName = item.name;
                    std::string newName = m_renameBuffer;
                    // An Input Action's name is what scripts call it, so it has to be a name they accept.
                    if (item.type == "InputAction" && !isValidActionName(newName)) {
                        const std::string fixed = actionNameFrom(newName);
                        OutputLog::info(LogCategory::Content, "Input Action names are used in scripts, so \"" + newName + "\" became \"" + fixed + "\".");
                        newName = fixed;
                    }
                    
                    if (!m_currentProjectPath.empty() && !item.filepath.empty()) {
                        try {
                            // Get the project directory (parent of the .pproj file)
                            std::filesystem::path projectFile(m_currentProjectPath);
                            std::filesystem::path projectDir = projectFile.parent_path();
                            
                            // Normalize the old filepath (convert forward slashes to native)
                            std::filesystem::path oldRelativePath(item.filepath);
                            oldRelativePath = oldRelativePath.make_preferred();
                            std::filesystem::path oldFilePath = projectDir / oldRelativePath;
                            
                            CrashHandler::logInfo("Attempting to rename asset:");
                            CrashHandler::logInfo("  Old name: " + oldName);
                            CrashHandler::logInfo("  New name: " + newName);
                            CrashHandler::logInfo("  Project file: " + m_currentProjectPath);
                            CrashHandler::logInfo("  Project dir: " + projectDir.string());
                            CrashHandler::logInfo("  Old filepath: " + item.filepath);
                            CrashHandler::logInfo("  Full old path: " + oldFilePath.string());
                            
                            // Get the file extension from the old filepath
                            std::string extension = oldRelativePath.extension().string();
                            CrashHandler::logInfo("  Extension: " + extension);
                            
                            // Create new filepath with native separators
                            std::filesystem::path newRelativePath = std::filesystem::path("Assets") / item.folder / (newName + extension);
                            std::filesystem::path newFilePath = projectDir / newRelativePath;
                            
                            CrashHandler::logInfo("  New filepath: " + newRelativePath.string());
                            CrashHandler::logInfo("  Full new path: " + newFilePath.string());
                            
                            // If old file doesn't exist, create it first
                            if (!std::filesystem::exists(oldFilePath)) {
                                CrashHandler::logWarning("Old file does not exist, creating it first: " + oldFilePath.string());
                                
                                // Create parent directories
                                std::filesystem::create_directories(oldFilePath.parent_path());
                                
                                // Create the file based on asset type
                                std::ofstream file(oldFilePath, std::ios::binary);
                                if (file.is_open()) {
                                    if (item.type == "Material") {
                                        file.write("MAT", 3);
                                        file.write(reinterpret_cast<const char*>(&item.materialColor.r), sizeof(float));
                                        file.write(reinterpret_cast<const char*>(&item.materialColor.g), sizeof(float));
                                        file.write(reinterpret_cast<const char*>(&item.materialColor.b), sizeof(float));
                                    } else if (item.type == "Object") {
                                        ObjectFile::write(file, ObjectFile::dataOf(item));
                                    }
                                    file.close();
                                    CrashHandler::logInfo("Created missing file before rename");
                                }
                            }
                            
                            // Rename file on disk
                            if (std::filesystem::exists(oldFilePath)) {
                                std::filesystem::create_directories(newFilePath.parent_path());
                                std::filesystem::rename(oldFilePath, newFilePath);
                                CrashHandler::logInfo("Successfully renamed asset file on disk");
                            } else {
                                CrashHandler::logError("Failed to create or find old file for rename");
                            }
                            
                            // Update item properties
                            if (item.type == "HDRI") {
                                const std::string oldEnvironmentPath = "Assets/" + item.folder + "/" + oldRelativePath.filename().string();
                                if (m_environment.hdriPath == oldEnvironmentPath) {
                                    m_environment.hdriPath = "Assets/" + item.folder + "/" + newRelativePath.filename().string();
                                    markProjectModified();
                                }
                            }
                            item.name = newName;
                            item.filepath = newRelativePath.string();
                            
                        } catch (const std::exception& e) {
                            std::string error = "Failed to rename asset file: " + std::string(e.what());
                            CrashHandler::logError(error);
                        }
                    } else {
                        // Just update the name if no file exists
                        item.name = newName;
                    }
                    
                    // A mesh's ID comes from its path, so renaming it gives it a new ID. Point everything that used the old
                    // one (Objects, placed instances, open tabs) at the new one, so none of them lose their mesh.
                    if (item.type == "Mesh") {
                        const std::string newId = PropulsionMKI::Utils::meshAssetGUID(item.filepath, item.name);
                        const std::string oldId = item.id;
                        item.id = newId;
                        retargetMeshReferences(oldId, newId);
                    }

                    // Update any open tabs with this asset
                    for (auto& tab : m_tabs) {
                        if (tab.type == TabType::AssetEditor && tab.contentItemIndex == m_renamingItemIndex) {
                            tab.name = item.name;
                        }
                    }
                    if (item.type == "InputAction") {
                        item.inputAction.name = item.name;
                        syncScriptInputs();  // scripts that used the old name no longer compile: say so
                        if (oldName != item.name) OutputLog::info(LogCategory::Content, "Renamed Input Action " + oldName + " to " + item.name + ": scripts that use \"" + oldName + "\" need the new name.");
                    }
                }
                
                m_showRenamePopup = false;
                m_renamingItemIndex = -1;
                m_renamingFolderIndex = -1;
                m_renamingFolder = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(PropulsionMKI::UI::scaled(120), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_showRenamePopup = false;
            m_renamingItemIndex = -1;
            m_renamingFolderIndex = -1;
            m_renamingFolder = false;
        }
        
        // Auto-focus on the input field when popup opens
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(-1);
        }
        
        ImGui::EndPopup();
    }
    
    ImGui::EndChild();  // End Assets child
    
    ImGui::End();
}

void Engine::createMaterial(const std::string& name) {
    // Save material file to disk (filesystem is source of truth)
    if (!m_currentProjectPath.empty()) {
        try {
            std::string filepath = "Assets\\" + m_currentFolder + "\\" + name + ".pmat";
            CrashHandler::logInfo("Creating material file: " + filepath);
            
            std::filesystem::path projectFile(m_currentProjectPath);
            std::filesystem::path projectDir = projectFile.parent_path();
            std::filesystem::path materialFile = projectDir / filepath;
            
            OutputLog::print(LogCategory::Content, LogLevel::Info, "Saving material to: ", materialFile.string());
            
            // Create parent directories if they don't exist
            std::filesystem::create_directories(materialFile.parent_path());
            
            // Default gray material color
            glm::vec3 defaultColor(0.7f, 0.7f, 0.7f);
            
            // Generate unique ID
            std::string guid = generateGUID();
            size_t guidLength = guid.length();
            
            std::ofstream matFile(materialFile, std::ios::binary);
            if (matFile.is_open()) {
                // Write header with version
                matFile.write("MAT", 3);
                int formatMarker = 1;
                matFile.write(reinterpret_cast<const char*>(&formatMarker), sizeof(int));
                
                // Write GUID
                matFile.write(reinterpret_cast<const char*>(&guidLength), sizeof(size_t));
                matFile.write(guid.c_str(), guidLength);
                
                // Write color
                matFile.write(reinterpret_cast<const char*>(&defaultColor.r), sizeof(float));
                matFile.write(reinterpret_cast<const char*>(&defaultColor.g), sizeof(float));
                matFile.write(reinterpret_cast<const char*>(&defaultColor.b), sizeof(float));
                
                // Write empty node data (will be populated when material is edited)
                size_t nodeCount = 0;
                matFile.write(reinterpret_cast<const char*>(&nodeCount), sizeof(size_t));
                
                // Write empty connections
                size_t connCount = 0;
                matFile.write(reinterpret_cast<const char*>(&connCount), sizeof(size_t));
                
                // Write empty thumbnail
                uint32_t thumbWidth = 0, thumbHeight = 0;
                size_t pixelDataSize = 0;
                matFile.write(reinterpret_cast<const char*>(&thumbWidth), sizeof(uint32_t));
                matFile.write(reinterpret_cast<const char*>(&thumbHeight), sizeof(uint32_t));
                matFile.write(reinterpret_cast<const char*>(&pixelDataSize), sizeof(size_t));
                
                // Write default splitter ratio
                float splitterRatio = 0.5f;
                matFile.write(reinterpret_cast<const char*>(&splitterRatio), sizeof(float));
                
                matFile.close();
                OutputLog::print(LogCategory::Content, LogLevel::Info, "Material file saved successfully");
                CrashHandler::logInfo("Material file saved successfully: " + materialFile.string());
                
                // Rescan ALL folders to pick up the new file and keep meshes available
                scanAssetsDirectory(true);
                
                // Find the newly created material and open it in a tab
                int materialIndex = -1;
                for (size_t i = 0; i < m_contentItems.size(); i++) {
                    if (m_contentItems[i].name == name && m_contentItems[i].type == "Material") {
                        materialIndex = static_cast<int>(i);
                        break;
                    }
                }
                
                if (materialIndex >= 0) {
                    Tab newTab(name, TabType::AssetEditor, materialIndex);
                    newTab.assetId = m_contentItems[materialIndex].id;
                    m_tabs.push_back(newTab);
                    m_activeTabIndex = static_cast<int>(m_tabs.size() - 1);
                }
                
                OutputLog::info(LogCategory::Content, "Created material: " + name);
            } else {
                std::string error = "Failed to open material file for writing: " + materialFile.string();
                CrashHandler::logError(error);
            }
        } catch (const std::exception& e) {
            std::string error = "Exception saving material file: " + std::string(e.what());
            CrashHandler::logError(error);
        }
    } else {
        OutputLog::print(LogCategory::Content, LogLevel::Warning, "Cannot create material - project not saved yet");
    }
}

void Engine::createObject(const std::string& name, int lightType) {
    // Save object file to disk (filesystem is source of truth)
    if (!m_currentProjectPath.empty()) {
        try {
            std::string filepath = "Assets\\" + m_currentFolder + "\\" + name + ".pobj";
            CrashHandler::logInfo("Creating object file: " + filepath);
            
            std::filesystem::path projectFile(m_currentProjectPath);
            std::filesystem::path projectDir = projectFile.parent_path();
            std::filesystem::path objectFile = projectDir / filepath;
            
            OutputLog::print(LogCategory::Content, LogLevel::Info, "Saving object to: ", objectFile.string());
            
            // Create parent directories if they don't exist
            std::filesystem::create_directories(objectFile.parent_path());
            
            // Generate unique ID
            std::string guid = generateGUID();
            
            // Default: Cube mesh, no material
            std::string defaultMeshGUID;
            std::string defaultMaterialGUID;
            
            // Find the Cube mesh GUID
            for (const auto& item : m_contentItems) {
                if (item.type == "Mesh" && item.name == "Cube") {
                    defaultMeshGUID = item.id;
                    break;
                }
            }
            
            std::ofstream objFile(objectFile, std::ios::binary);
            if (objFile.is_open()) {
                // A new Object has a Cube mesh, no material, and no script (it just exists)
                ObjectFile::Data data{guid, defaultMeshGUID, defaultMaterialGUID, ""};
                if (lightType >= 0) {
                    // A light is just a light: no mesh to draw (the editor shows its own picture of it)
                    data.meshGUID.clear();
                    data.light = lightType == 0 ? LightSettings::directional() : LightSettings::point();
                }
                ObjectFile::write(objFile, data);

                objFile.close();
                OutputLog::print(LogCategory::Content, LogLevel::Info, "Object file saved successfully (v", int(ObjectFile::kVersion), " format)");
                CrashHandler::logInfo("Object file saved successfully: " + objectFile.string());
                
                // Rescan ALL folders to pick up the new file and keep meshes/materials available
                scanAssetsDirectory(true);
                
                // Find the newly created object and open it in a tab
                int objectIndex = -1;
                for (size_t i = 0; i < m_contentItems.size(); i++) {
                    if (m_contentItems[i].name == name && m_contentItems[i].type == "Object") {
                        objectIndex = static_cast<int>(i);
                        break;
                    }
                }
                
                if (objectIndex >= 0) {
                    Tab newTab(name, TabType::AssetEditor, objectIndex);
                    newTab.assetId = m_contentItems[objectIndex].id;
                    m_tabs.push_back(newTab);
                    m_activeTabIndex = static_cast<int>(m_tabs.size() - 1);
                }
                
                OutputLog::info(LogCategory::Content, "Created object: " + name);
            } else {
                std::string error = "Failed to open object file for writing: " + objectFile.string();
                CrashHandler::logError(error);
            }
        } catch (const std::exception& e) {
            std::string error = "Exception saving object file: " + std::string(e.what());
            CrashHandler::logError(error);
        }
    } else {
        OutputLog::print(LogCategory::Content, LogLevel::Warning, "Cannot create object - project not saved yet");
    }
}

std::string Engine::getAssetTypeFromExtension(const std::string& extension) {
    std::string ext = extension;
    // Convert to lowercase for comparison
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // Image files
    // HDRI panoramas (the sky)
    if (ext == ".hdr") {
        return "HDRI";
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
        return "Texture";
    }
    // 3D model files  
    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb") {
        return "Mesh";
    }
    // Video files
    if (ext == ".mp4" || ext == ".avi" || ext == ".mov" || ext == ".mkv") {
        return "Video";
    }
    // Audio files
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
        return "Audio";
    }
    
    return "Unknown";
}

VkDescriptorSet Engine::generateTextureThumbnail(const std::string& filepath) {
    if (!m_thumbnailGenerator) {
        return VK_NULL_HANDLE;
    }
    
    // Use stb_image to load the texture
    int width, height, channels;
    unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 4);
    
    if (!data) {
        CrashHandler::logError("Failed to load texture for thumbnail: " + filepath);
        return VK_NULL_HANDLE;
    }
    
    // Generate thumbnail using the thumbnail generator's method
    VkDescriptorSet thumbnail = m_thumbnailGenerator->generateTextureThumbnail(data, width, height, 128);
    
    stbi_image_free(data);
    return thumbnail;
}

void Engine::importFile(const std::string& filepath) {
    if (m_currentProjectPath.empty()) {
        CrashHandler::logWarning("Cannot import file - no project open");
        OutputLog::error(LogCategory::Content, "Cannot import file - please save the project first");
        return;
    }
    
    try {
        std::filesystem::path sourcePath(filepath);
        std::string filename = sourcePath.stem().string();
        std::string extension = sourcePath.extension().string();
        std::string assetType = getAssetTypeFromExtension(extension);
        
        if (assetType == "Unknown") {
            CrashHandler::logWarning("Unsupported file type: " + extension);
            OutputLog::error(LogCategory::Content, "Unsupported file type: " + extension);
            return;
        }
        
        // Determine target folder based on asset type
        std::string targetFolder = m_currentFolder;  // Use current folder by default
        if (targetFolder.empty()) {
            // If no folder selected, put in appropriate default folder
            if (assetType == "Texture") targetFolder = "Textures";
            else if (assetType == "Mesh") targetFolder = "Meshes";
            else if (assetType == "Video") targetFolder = "Videos";
            else if (assetType == "Audio") targetFolder = "Audio";
            else targetFolder = "Assets";
        }
        
        // HDRIs (the sky) live in their own folder
        if (assetType == "HDRI") targetFolder = "HDRI";

        // Calculate destination path
        std::filesystem::path projectFile(m_currentProjectPath);
        std::filesystem::path projectDir = projectFile.parent_path();
        std::filesystem::path destFolder = projectDir / "Assets" / targetFolder;
        std::filesystem::path destPath = destFolder / sourcePath.filename();
        
        // Create destination folder if it doesn't exist
        std::filesystem::create_directories(destFolder);
        
        // Check if file already exists
        if (std::filesystem::exists(destPath)) {
            // Append number to filename to make it unique
            int counter = 1;
            while (std::filesystem::exists(destPath)) {
                std::string newName = filename + "_" + std::to_string(counter) + extension;
                destPath = destFolder / newName;
                counter++;
            }
            filename = destPath.stem().string();
        }
        
        // Copy file to destination
        std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing);
        CrashHandler::logInfo("Imported file: " + sourcePath.string() + " -> " + destPath.string());
        
        // Create ContentItem for the imported file
        std::string relativePath = "Assets/" + targetFolder + "/" + destPath.filename().string();
        ContentItem newItem(filename, assetType, targetFolder, relativePath);
        if (assetType != "Mesh") newItem.id = generateGUID();  // a mesh's ID comes from its path, so it is the same after a reload
        
        // Store full path for texture loading
        newItem.filepath = destPath.string();
        
        // Generate appropriate thumbnail based on type
        if (assetType == "Texture" || assetType == "HDRI") {
            newItem.thumbnailTexture = generateTextureThumbnail(destPath.string());
        } else if (assetType == "Mesh") {
            // Load mesh and generate thumbnail using MeshLoader
            std::unique_ptr<Mesh> mesh = MeshLoader::loadFromFile(m_vulkanContext.get(), destPath.string());
            if (mesh) {
                newItem.thumbnailTexture = m_thumbnailGenerator->generateMeshThumbnail(mesh.get(), 128);
                CrashHandler::logInfo("Generated mesh thumbnail for: " + filename);
            } else {
                CrashHandler::logWarning("Failed to load mesh for thumbnail: " + destPath.string());
            }
        } else if (assetType == "Video" || assetType == "Audio") {
            // For now, use a placeholder color thumbnail for video/audio
            glm::vec3 placeholderColor = (assetType == "Video") ? glm::vec3(0.2f, 0.4f, 0.8f) : glm::vec3(0.8f, 0.4f, 0.2f);
            newItem.thumbnailTexture = m_thumbnailGenerator->generateMaterialThumbnail(placeholderColor, 128);
        }
        
        // Add to content items
        m_contentItems.push_back(newItem);
        
        // Make sure folder exists in folder list
        bool folderExists = false;
        for (const auto& folder : m_folders) {
            if (folder == targetFolder) {
                folderExists = true;
                break;
            }
        }
        if (!folderExists) {
            m_folders.push_back(targetFolder);
        }
        
        // Switch to the target folder
        m_currentFolder = targetFolder;
        for (size_t i = 0; i < m_folders.size(); i++) {
            if (m_folders[i] == targetFolder) {
                m_selectedFolderIndex = static_cast<int>(i);
                break;
            }
        }
        
        OutputLog::info(LogCategory::Content, "Imported: " + filename + " (" + assetType + ")");
        CrashHandler::logInfo("Successfully imported " + assetType + ": " + filename);
        
    } catch (const std::exception& e) {
        std::string error = "Failed to import file: " + std::string(e.what());
        CrashHandler::logError(error);
        OutputLog::error(LogCategory::Content, error);
    }
}

} // namespace Propulsion
