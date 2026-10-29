#include "Engine.h"
#include "ui/ImGuiTheme.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "ui/IconsFontAwesome7.h"

namespace Propulsion {

void Engine::renderUI() {
    // Apply a UI scale change requested last frame (never mid-frame: fonts/style must be stable while drawing)
    if (m_uiScaleDirty) {
        applyUIScale();
        m_uiScaleDirty = false;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Render startup dialog first - blocks everything else until a choice is made
    if (m_showStartupDialog) {
        renderStartupDialog();
        
        // Still need to render ImGui
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_vulkanContext->getCurrentCommandBuffer());
        return; // Don't render rest of UI until project is chosen
    }
    
    // Render project name dialog if needed (also blocks main UI)
    if (m_showProjectNameDialog) {
        renderProjectNameDialog();
        
        // Still need to render ImGui
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_vulkanContext->getCurrentCommandBuffer());
        return;
    }
    
    // Render preferences window (non-blocking)
    renderPreferencesWindow();
    
    // Main menu bar
    renderMenuBar();
    
    // Create dockspace
    renderTabs();
    
    // Dockspace - position it right after menu bar and tab bar
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float menuBarHeight = ImGui::GetFrameHeight();
    float totalTopHeight = menuBarHeight + m_tabBarHeight;  // Use actual measured tab bar height
    float footerHeight = ImGui::GetFrameHeight();  // Footer at bottom of window
    
    // Only render DockSpace for Scene tab
    bool isSceneTab = m_activeTabIndex >= 0 && m_activeTabIndex < static_cast<int>(m_tabs.size()) &&
                      m_tabs[m_activeTabIndex].type == TabType::Scene;
    
    if (isSceneTab) {
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + totalTopHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - totalTopHeight - footerHeight));
        ImGui::SetNextWindowViewport(viewport->ID);
        
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                        ImGuiWindowFlags_NoNavFocus;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        ImGui::Begin("DockSpace", nullptr, window_flags);
        ImGui::PopStyleVar(3);
        
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        
        // Setup default docking layout on first frame or reset
        if (m_firstFrame) {
            m_firstFrame = false;
            m_dockingLayoutInitialized = false;
            m_lastDockSpaceSize = viewport->Size;
        }
        
        // Check if window size changed significantly (more than 10 pixels)
        bool sizeChanged = (fabs(viewport->Size.x - m_lastDockSpaceSize.x) > 10.0f || 
                            fabs(viewport->Size.y - m_lastDockSpaceSize.y) > 10.0f);
        
        // Check if we need to initialize or rebuild the layout
        bool shouldRebuildLayout = false;
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);
        
        if (!m_dockingLayoutInitialized || node == nullptr) {
            shouldRebuildLayout = true;
            m_dockingLayoutInitialized = true;
        } else if (sizeChanged) {
            // Rebuild layout to maintain proportions on resize
            shouldRebuildLayout = true;
        }
        
        // Update stored size
        if (sizeChanged) {
            m_lastDockSpaceSize = viewport->Size;
        }
        
        // Rebuild layout if needed
        if (shouldRebuildLayout) {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);
            
            // Split layout: Content Browser (25% bottom), Objects (20% right), Viewport (rest)
            ImGuiID dock_main_id = dockspace_id;
            ImGuiID dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);
            ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.20f, nullptr, &dock_main_id);
            
            // Dock windows
            ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
            ImGui::DockBuilderDockWindow("Objects", dock_right_id);
            ImGui::DockBuilderDockWindow("Content Browser", dock_bottom_id);
            ImGui::DockBuilderDockWindow("Output Log", dock_bottom_id);  // a tab beside the Content Browser
            
            ImGui::DockBuilderFinish(dockspace_id);
        }
        
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        
        ImGui::End();
    }
    
    // Render active tab content
    if (m_activeTabIndex >= 0 && m_activeTabIndex < static_cast<int>(m_tabs.size())) {
        Tab& activeTab = m_tabs[m_activeTabIndex];
        if (activeTab.type == TabType::Scene) {
            renderSceneTab();
        } else if (activeTab.type == TabType::ScriptGraph) {
            renderScriptGraphTab(activeTab);
        } else {
            renderAssetEditorTab(activeTab);
        }
    }
    
    // Always show side panels for scene tab
    if (m_activeTabIndex == 0) {
        // Conditionally render panels based on window menu toggles
        if (m_showObjectsPanel) {
            renderObjectsPanel();
        }
        
        if (m_showContentBrowser) {
            renderContentBrowser();
        }

        if (m_showOutputLog) {
            m_outputLog.draw(&m_showOutputLog);
            if (!m_showOutputLog) savePreferences();  // closed with its X
        }
    }
    
    // Render exit confirmation dialog (modal, can appear over anything)
    renderExitConfirmationDialog();
    
    // Render tab close confirmation dialog
    renderTabCloseConfirmationDialog();
    
    // Render persistent footer at the bottom of the window
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        float footerHeight = ImGui::GetFrameHeight();
        
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - footerHeight));
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x, footerHeight));
        ImGui::SetNextWindowViewport(vp->ID);
        
        ImGuiWindowFlags footerFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                        ImGuiWindowFlags_NoNav;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PropulsionMKI::UI::scaled(8.0f), PropulsionMKI::UI::scaled(2.0f)));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        
        ImGui::Begin("##Footer", nullptr, footerFlags);
        
        // Left-justify the version text
        const char* versionText = "Propulsion MK I v0.1.0-alpha";
        ImGui::TextUnformatted(versionText);

        // What the Output Log has caught, in the footer so a problem isn't missed with the panel closed. Click to open it.
        {
            const size_t errors = m_outputLog.errorsSinceClear();
            const size_t warnings = m_outputLog.warningsSinceClear();
            char status[96];
            std::snprintf(status, sizeof(status), "Output Log: %zu error%s, %zu warning%s", errors, errors == 1 ? "" : "s",
                          warnings, warnings == 1 ? "" : "s");
            const ImVec4 color = errors > 0 ? ImVec4(1.00f, 0.38f, 0.38f, 1.0f)
                               : warnings > 0 ? ImVec4(1.00f, 0.84f, 0.30f, 1.0f)
                               : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImGui::SameLine(0.0f, PropulsionMKI::UI::scaled(24.0f));
            ImGui::TextColored(color, "%s", status);
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                PropulsionMKI::UI::tooltip("Open the Output Log");
            }
            if (ImGui::IsItemClicked()) {
                m_showOutputLog = true;
                ImGui::SetWindowFocus("Output Log");
                savePreferences();
            }
        }
        
        // Right-justify the copyright text
        const char* copyrightText = "Copyright (c) 2025 Forgeworks Interactive Limited. All rights reserved.";
        float textWidth = ImGui::CalcTextSize(copyrightText).x;
        float windowWidth = ImGui::GetWindowSize().x;
        float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(windowWidth - textWidth - padding);
        ImGui::TextUnformatted(copyrightText);
        
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
    
    // Render ImGui
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_vulkanContext->getCurrentCommandBuffer());
}

void Engine::renderObjectsPanel() {
    ImGui::Begin("Objects");

    // A name that was being typed and is let go of is one step in the undo history (not one per letter).
    if (m_pendingNameEdit.fieldId != 0 && ImGui::GetActiveID() != m_pendingNameEdit.fieldId) {
        if (m_pendingNameEdit.before) recordObjectEdit(m_pendingNameEdit.index, *m_pendingNameEdit.before);
        m_pendingNameEdit = PendingEdit();
    }
    
    renderEnvironmentSection();

    ImGui::Text("Scene Objects:");
    ImGui::Separator();
    
    int objectToDelete = -1;
    int objectToDuplicate = -1;
    for (size_t i = 0; i < m_sceneObjects.size(); i++) {
        SceneObject& obj = m_sceneObjects[i];
        
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (m_selectedObjectIndex == static_cast<int>(i)) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        
        ImGui::TreeNodeEx((void*)(intptr_t)i, flags, "%s", obj.name.c_str());
        
        if (ImGui::IsItemClicked()) {
            m_selectedObjectIndex = static_cast<int>(i);
        }
        
        // Context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete", "Del")) {
                objectToDelete = static_cast<int>(i);
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                objectToDuplicate = static_cast<int>(i);
            }
            ImGui::EndPopup();
        }
    }
    
    // Delete removes the selected object while this panel is focused (the viewport handles it too).
    if (objectToDelete < 0 && m_selectedObjectIndex >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        objectToDelete = m_selectedObjectIndex;
    }
    if (objectToDelete >= 0) {
        deleteObject(objectToDelete);
    }

    // Ctrl+D duplicates the selected object while this panel is focused (the viewport handles it too).
    if (objectToDuplicate < 0 && objectToDelete < 0 && m_selectedObjectIndex >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        objectToDuplicate = m_selectedObjectIndex;
    }
    if (objectToDuplicate >= 0) {
        duplicateObject(objectToDuplicate);
    }
    
    ImGui::Separator();
    
    // Show selected object properties
    if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
        ImGui::Spacing();
        ImGui::Text("Properties:");
        ImGui::Separator();
        
        SceneObject& obj = m_sceneObjects[m_selectedObjectIndex];
        const SceneObject before = obj;  // how it is before anything below changes it: what Ctrl+Z goes back to
        
        char nameBuf[256];
        strncpy(nameBuf, obj.name.c_str(), sizeof(nameBuf));
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
            obj.name = nameBuf;
        }
        if (ImGui::IsItemActivated()) {
            m_pendingNameEdit.fieldId = ImGui::GetItemID();
            m_pendingNameEdit.index = m_selectedObjectIndex;
            m_pendingNameEdit.before = std::make_shared<SceneObject>(before);
        }
        
        // Position
        ImGui::Text("Position");
        ImGui::PushItemWidth(PropulsionMKI::UI::scaled(60.0f));
        
        static bool posXEditing = false;
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "X:"); ImGui::SameLine();
        ImGui::DragFloat("##PosX", &obj.position.x, 0.1f, 0.0f, 0.0f, "%.3f");
        if (ImGui::IsItemActivated()) { pushUndoState(); posXEditing = true; }
        if (posXEditing && !ImGui::IsItemActive()) { posXEditing = false; }
        ImGui::SameLine();
        
        static bool posYEditing = false;
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Y:"); ImGui::SameLine();
        ImGui::DragFloat("##PosY", &obj.position.y, 0.1f, 0.0f, 0.0f, "%.3f");
        if (ImGui::IsItemActivated()) { pushUndoState(); posYEditing = true; }
        if (posYEditing && !ImGui::IsItemActive()) { posYEditing = false; }
        ImGui::SameLine();
        
        static bool posZEditing = false;
        ImGui::TextColored(ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "Z:"); ImGui::SameLine();
        ImGui::DragFloat("##PosZ", &obj.position.z, 0.1f, 0.0f, 0.0f, "%.3f");
        if (ImGui::IsItemActivated()) { pushUndoState(); posZEditing = true; }
        if (posZEditing && !ImGui::IsItemActive()) { posZEditing = false; }
        ImGui::PopItemWidth();
        
        // Rotation
        ImGui::Text("Rotation");
        ImGui::PushItemWidth(PropulsionMKI::UI::scaled(60.0f));
        
        static bool rotXEditing = false;
        static float rotXStart = 0.0f;
        static glm::quat rotXStartQuat(1.0f, 0.0f, 0.0f, 0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "X:"); ImGui::SameLine();
        float rotXValue = obj.rotation.x;
        ImGui::DragFloat("##RotX", &rotXValue, 1.0f, 0.0f, 0.0f, "%.1f");
        if (ImGui::IsItemActivated()) { 
            pushUndoState(); 
            rotXEditing = true; 
            rotXStart = obj.rotation.x;
            rotXStartQuat = obj.quaternion;
        }
        if (ImGui::IsItemActive()) {
            float delta = rotXValue - rotXStart;
            glm::quat deltaQuat = glm::angleAxis(glm::radians(delta), glm::vec3(1.0f, 0.0f, 0.0f));
            obj.quaternion = deltaQuat * rotXStartQuat;
            syncEulerFromQuaternion(obj);
            obj.rotation.x = rotXValue;
        }
        if (rotXEditing && !ImGui::IsItemActive()) { rotXEditing = false; }
        ImGui::SameLine();
        
        static bool rotYEditing = false;
        static float rotYStart = 0.0f;
        static glm::quat rotYStartQuat(1.0f, 0.0f, 0.0f, 0.0f);
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Y:"); ImGui::SameLine();
        float rotYValue = obj.rotation.y;
        ImGui::DragFloat("##RotY", &rotYValue, 1.0f, 0.0f, 0.0f, "%.1f");
        if (ImGui::IsItemActivated()) { 
            pushUndoState(); 
            rotYEditing = true; 
            rotYStart = obj.rotation.y;
            rotYStartQuat = obj.quaternion;
        }
        if (ImGui::IsItemActive()) {
            float delta = rotYValue - rotYStart;
            glm::quat deltaQuat = glm::angleAxis(glm::radians(delta), glm::vec3(0.0f, 1.0f, 0.0f));
            obj.quaternion = deltaQuat * rotYStartQuat;
            syncEulerFromQuaternion(obj);
            obj.rotation.y = rotYValue;
        }
        if (rotYEditing && !ImGui::IsItemActive()) { rotYEditing = false; }
        ImGui::SameLine();
        
        static bool rotZEditing = false;
        static float rotZStart = 0.0f;
        static glm::quat rotZStartQuat(1.0f, 0.0f, 0.0f, 0.0f);
        ImGui::TextColored(ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "Z:"); ImGui::SameLine();
        float rotZValue = obj.rotation.z;
        ImGui::DragFloat("##RotZ", &rotZValue, 1.0f, 0.0f, 0.0f, "%.1f");
        if (ImGui::IsItemActivated()) { 
            pushUndoState(); 
            rotZEditing = true; 
            rotZStart = obj.rotation.z;
            rotZStartQuat = obj.quaternion;
        }
        if (ImGui::IsItemActive()) {
            float delta = rotZValue - rotZStart;
            glm::quat deltaQuat = glm::angleAxis(glm::radians(delta), glm::vec3(0.0f, 0.0f, 1.0f));
            obj.quaternion = deltaQuat * rotZStartQuat;
            syncEulerFromQuaternion(obj);
            obj.rotation.z = rotZValue;
        }
        if (rotZEditing && !ImGui::IsItemActive()) { rotZEditing = false; }
        ImGui::PopItemWidth();
        
        // Scale
        ImGui::Text("Scale");
        
        ImGui::PushItemWidth(PropulsionMKI::UI::scaled(60.0f));
        
        // Uniform scale lock state
        static bool uniformScaleLock = false;
        static glm::vec3 scaleStartValues(1.0f);
        
        static bool scaleXEditing = false;
        static float scaleStartValue = 1.0f;
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "X:"); ImGui::SameLine();
        float scaleXValue = obj.scale.x;
        ImGui::DragFloat("##ScaleX", &scaleXValue, 0.01f, 0.0f, 0.0f, "%.3f");
        if (ImGui::IsItemActivated()) { 
            pushUndoState(); 
            scaleXEditing = true;
            scaleStartValue = obj.scale.x;
            scaleStartValues = obj.scale;
        }
        if (ImGui::IsItemActive() && uniformScaleLock) {
            float scaleFactor = scaleXValue / scaleStartValue;
            obj.scale.x = scaleStartValues.x * scaleFactor;
            obj.scale.y = scaleStartValues.y * scaleFactor;
            obj.scale.z = scaleStartValues.z * scaleFactor;
        } else if (ImGui::IsItemActive()) {
            obj.scale.x = scaleXValue;
        }
        if (scaleXEditing && !ImGui::IsItemActive()) { scaleXEditing = false; }
        ImGui::SameLine();
        
        static bool scaleYEditing = false;
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Y:"); ImGui::SameLine();
        float scaleYValue = obj.scale.y;
        ImGui::DragFloat("##ScaleY", &scaleYValue, 0.01f, 0.0f, 0.0f, "%.3f");
        if (ImGui::IsItemActivated()) { 
            pushUndoState(); 
            scaleYEditing = true;
            scaleStartValue = obj.scale.y;
            scaleStartValues = obj.scale;
        }
        if (ImGui::IsItemActive() && uniformScaleLock) {
            float scaleFactor = scaleYValue / scaleStartValue;
            obj.scale.x = scaleStartValues.x * scaleFactor;
            obj.scale.y = scaleStartValues.y * scaleFactor;
            obj.scale.z = scaleStartValues.z * scaleFactor;
        } else if (ImGui::IsItemActive()) {
            obj.scale.y = scaleYValue;
        }
        if (scaleYEditing && !ImGui::IsItemActive()) { scaleYEditing = false; }
        ImGui::SameLine();
        
        static bool scaleZEditing = false;
        ImGui::TextColored(ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "Z:"); ImGui::SameLine();
        float scaleZValue = obj.scale.z;
        ImGui::DragFloat("##ScaleZ", &scaleZValue, 0.01f, 0.0f, 0.0f, "%.3f");
        if (ImGui::IsItemActivated()) { 
            pushUndoState(); 
            scaleZEditing = true;
            scaleStartValue = obj.scale.z;
            scaleStartValues = obj.scale;
        }
        if (ImGui::IsItemActive() && uniformScaleLock) {
            float scaleFactor = scaleZValue / scaleStartValue;
            obj.scale.x = scaleStartValues.x * scaleFactor;
            obj.scale.y = scaleStartValues.y * scaleFactor;
            obj.scale.z = scaleStartValues.z * scaleFactor;
        } else if (ImGui::IsItemActive()) {
            obj.scale.z = scaleZValue;
        }
        if (scaleZEditing && !ImGui::IsItemActive()) { scaleZEditing = false; }
        
        // Uniform scale lock button
        ImGui::SameLine();
        ImGui::PushFont(m_fontRegular); // Use Space Mono Regular with Font Awesome icons merged
        if (uniformScaleLock) {
            if (ImGui::Button(ICON_FA_LOCK "##UniformScale", ImVec2(PropulsionMKI::UI::scaled(30), PropulsionMKI::UI::scaled(20)))) {
                uniformScaleLock = false;
            }
            if (ImGui::IsItemHovered()) {
                PropulsionMKI::UI::tooltip("Uniform scaling enabled. Click to unlock.");
            }
        } else {
            if (ImGui::Button(ICON_FA_UNLOCK "##UniformScale", ImVec2(PropulsionMKI::UI::scaled(30), PropulsionMKI::UI::scaled(20)))) {
                uniformScaleLock = true;
            }
            if (ImGui::IsItemHovered()) {
                PropulsionMKI::UI::tooltip("Uniform scaling disabled. Click to lock.");
            }
        }
        ImGui::PopFont();
        
        ImGui::PopItemWidth();
        
        if (ImGui::Checkbox("Visible", &obj.visible)) recordObjectEdit(m_selectedObjectIndex, before);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Instance Properties");
        
        // An instance follows its Object asset's mesh and material until one is set here.
        // "(default)" = following the asset; "(override)" = set on this instance, with a button to go back.
        const ContentItem* sourceAsset = sourceAssetOf(obj);
        if (sourceAsset) {
            ImGui::TextDisabled("From Object asset: %s", sourceAsset->name.c_str());
        }

        // Mesh selection dropdown
        ImGui::Text("Mesh:");
        ImGui::SameLine();

        // Get current mesh name (GUID first, as drawing does)
        std::string currentMeshName = "None";
        int currentMeshItem = -1;
        if (!obj.meshGUID.empty()) {
            for (size_t i = 0; i < m_contentItems.size(); i++) {
                if (m_contentItems[i].type == "Mesh" && m_contentItems[i].id == obj.meshGUID) {
                    currentMeshItem = static_cast<int>(i);
                    break;
                }
            }
        } else if (obj.meshIndex >= 0 && obj.meshIndex < static_cast<int>(m_contentItems.size())) {
            currentMeshItem = obj.meshIndex;
        }
        if (currentMeshItem >= 0) {
            currentMeshName = m_contentItems[currentMeshItem].name;
        }
        if (sourceAsset) currentMeshName += obj.overridesMesh ? " (override)" : " (default)";

        if (ImGui::BeginCombo("##MeshCombo", currentMeshName.c_str())) {
            // Show all mesh items
            for (size_t i = 0; i < m_contentItems.size(); i++) {
                if (m_contentItems[i].type == "Mesh") {
                    bool isSelected = (currentMeshItem == static_cast<int>(i));
                    
                    // Show thumbnail if available
                    if (m_contentItems[i].thumbnailTexture != VK_NULL_HANDLE) {
                        ImGui::Image((ImTextureID)m_contentItems[i].thumbnailTexture, 
                                   ImVec2(PropulsionMKI::UI::scaled(32), PropulsionMKI::UI::scaled(32)), ImVec2(0, 0), ImVec2(1, 1));
                        ImGui::SameLine();
                    }
                    
                    if (ImGui::Selectable(m_contentItems[i].name.c_str(), isSelected, 0, ImVec2(0, PropulsionMKI::UI::scaled(32)))) {
                        setInstanceMesh(obj, static_cast<int>(i));
                        recordObjectEdit(m_selectedObjectIndex, before);
                        markProjectModified();
                    }

                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (sourceAsset && obj.overridesMesh) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##Mesh")) {
                obj.overridesMesh = false;
                applyAssetDefaults(obj);
                recordObjectEdit(m_selectedObjectIndex, before);
                markProjectModified();
            }
            if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Go back to the Object asset's mesh");
        }

        // Material selection dropdown
        ImGui::Text("Material:");
        ImGui::SameLine();
        
        // Get current material name
        std::string currentMaterialName = "None";
        const int currentMaterialIndex = materialIndexOf(obj);
        if (currentMaterialIndex >= 0) {
            currentMaterialName = m_contentItems[currentMaterialIndex].name;
        }
        if (sourceAsset) currentMaterialName += obj.overridesMaterial ? " (override)" : " (default)";

        if (ImGui::BeginCombo("##MaterialCombo", currentMaterialName.c_str())) {
            // Show all material items
            for (size_t i = 0; i < m_contentItems.size(); i++) {
                if (m_contentItems[i].type == "Material") {
                    bool isSelected = (currentMaterialIndex == static_cast<int>(i));
                    
                    // Show thumbnail if available
                    if (m_contentItems[i].thumbnailTexture != VK_NULL_HANDLE) {
                        ImGui::Image((ImTextureID)m_contentItems[i].thumbnailTexture, 
                                   ImVec2(PropulsionMKI::UI::scaled(32), PropulsionMKI::UI::scaled(32)), ImVec2(0, 0), ImVec2(1, 1));
                        ImGui::SameLine();
                    }
                    
                    if (ImGui::Selectable(m_contentItems[i].name.c_str(), isSelected, 0, ImVec2(0, PropulsionMKI::UI::scaled(32)))) {
                        setInstanceMaterial(obj, static_cast<int>(i));
                        recordObjectEdit(m_selectedObjectIndex, before);
                        markProjectModified();
                    }

                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (sourceAsset && obj.overridesMaterial) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##Material")) {
                obj.overridesMaterial = false;
                applyAssetDefaults(obj);
                recordObjectEdit(m_selectedObjectIndex, before);
                markProjectModified();
            }
            if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Go back to the Object asset's material");
        }
    }
    
    ImGui::End();
}

void Engine::renderToolButtons() {
    // Save cursor position to draw at top-left of viewport
    ImVec2 buttonPos = ImVec2(m_viewportPos.x + 10, m_viewportPos.y + 10);
    
    ImGui::SetCursorScreenPos(buttonPos);
    
    // Button styling
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(PropulsionMKI::UI::scaled(4), PropulsionMKI::UI::scaled(4)));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    
    float buttonSize = PropulsionMKI::UI::scaled(40.0f);
    ImVec2 buttonSizeVec = ImVec2(buttonSize, buttonSize);
    
    // Move tool button
    bool wasTranslate = (m_gizmoMode == GizmoMode::Translate);
    if (wasTranslate) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.42f, 0.00f, 0.9f)); // Orange when active
    }
    
    if (ImGui::Button("M", buttonSizeVec)) {
        m_gizmoMode = (m_gizmoMode == GizmoMode::Translate) ? GizmoMode::None : GizmoMode::Translate;
    }
    
    if (wasTranslate) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        PropulsionMKI::UI::tooltip("Move Tool (W)");
    }
    
    ImGui::SameLine();
    
    // Rotate tool button
    bool wasRotate = (m_gizmoMode == GizmoMode::Rotate);
    if (wasRotate) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.42f, 0.00f, 0.9f)); // Orange when active
    }
    
    if (ImGui::Button("R", buttonSizeVec)) {
        m_gizmoMode = (m_gizmoMode == GizmoMode::Rotate) ? GizmoMode::None : GizmoMode::Rotate;
    }
    
    if (wasRotate) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        PropulsionMKI::UI::tooltip("Rotate Tool (E)");
    }
    
    ImGui::SameLine();
    
    // Scale tool button
    bool wasScale = (m_gizmoMode == GizmoMode::Scale);
    if (wasScale) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.42f, 0.00f, 0.9f)); // Orange when active
    }
    
    if (ImGui::Button("S", buttonSizeVec)) {
        m_gizmoMode = (m_gizmoMode == GizmoMode::Scale) ? GizmoMode::None : GizmoMode::Scale;
    }
    
    if (wasScale) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        PropulsionMKI::UI::tooltip("Scale Tool (R)");
    }
    
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
}

} // namespace Propulsion
