#include "Engine.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

namespace Propulsion {

void Engine::renderTabs() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    
    // Create a zero-height window just to host the tab bar
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 0));
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    
    ImGui::Begin("##TabBar", nullptr, flags);
    
    if (ImGui::BeginTabBar("MainTabBar", ImGuiTabBarFlags_Reorderable)) {
        // Only log when something interesting happens
        static int lastActiveTabIndex = -1;
        static size_t lastTabCount = 0;
        static std::vector<bool> lastUnsavedStates;
        

        
        // Track if ANY tab is currently open this frame
        int currentFrameActiveTab = -1;
        static bool needsForceSelection = false;
        static int tabToForceSelect = -1;
        
        for (size_t i = 0; i < m_tabs.size(); i++) {
            ImGuiTabItemFlags tabFlags = ImGuiTabItemFlags_None;
            
            // If we detected no tabs were open last frame, force-select the active tab
            if (needsForceSelection && static_cast<int>(i) == tabToForceSelect) {
                tabFlags |= ImGuiTabItemFlags_SetSelected;
                needsForceSelection = false;
            }
            
            // If this tab needs to be selected, set the flag and clear it
            // This takes priority over everything else
            if (m_tabs[i].needsSelection) {
                tabFlags |= ImGuiTabItemFlags_SetSelected;
                m_tabs[i].needsSelection = false;
                // When we explicitly select a new tab, update m_activeTabIndex immediately
                m_activeTabIndex = static_cast<int>(i);
            }
            
            // Build the tab label with stable ID
            // Format: "DisplayLabel###StableID"
            // Using ### (triple hash) means ONLY what comes after ### is used as the ID
            // This allows the display label to change (e.g., adding asterisk) without changing the tab ID
            // The ID is the tab's kind and the asset it shows, not just its name: a Material and an Object can share a name,
            // and two tabs with the same ID are an ImGui error (the tab can't be told apart, and clicks go to the wrong one).
            std::string displayLabel = (m_tabs[i].hasUnsavedChanges ? "* " : "") + m_tabs[i].name;
            std::string tabId = std::to_string(static_cast<int>(m_tabs[i].type)) + ":" +
                                (m_tabs[i].assetId.empty() ? m_tabs[i].name : m_tabs[i].assetId);
            std::string fullLabel = displayLabel + "###" + tabId;
            
            // First tab (scene tab) should not have a close button
            bool* pOpen = (i == 0) ? nullptr : new bool(true);
            
            bool isOpen = ImGui::BeginTabItem(fullLabel.c_str(), pOpen, tabFlags);
            
            // Track if this tab is currently selected
            if (isOpen) {
                currentFrameActiveTab = static_cast<int>(i);
                ImGui::EndTabItem();
            }
            
            // Handle tab close (only for non-scene tabs)
            bool open = pOpen ? *pOpen : true;
            if (pOpen) delete pOpen;
            
            // Handle tab close
            if (!open && i != 0) {
                // Check if tab has unsaved changes
                if (m_tabs[i].hasUnsavedChanges) {
                    // Show confirmation dialog
                    m_showTabCloseConfirmation = true;
                    m_tabToClose = static_cast<int>(i);
                } else {
                    // No unsaved changes, close immediately
                    // Remove from unsaved tracking if present
                    m_unsavedContentItems.erase(m_tabs[i].contentItemIndex);
                    m_tabs.erase(m_tabs.begin() + i);
                    if (m_activeTabIndex >= static_cast<int>(m_tabs.size())) {
                        m_activeTabIndex = static_cast<int>(m_tabs.size()) - 1;
                    }
                }
                break;
            }
        }
        
        // Only update m_activeTabIndex if at least one tab was open this frame
        // If no tabs are open (happens when label changes), preserve the current active tab
        if (currentFrameActiveTab != -1) {
            m_activeTabIndex = currentFrameActiveTab;
        } else {
            // Set flag to force-select the active tab on next frame
            needsForceSelection = true;
            tabToForceSelect = m_activeTabIndex;
        }
        
        // Update tracking state
        lastActiveTabIndex = m_activeTabIndex;
        lastTabCount = m_tabs.size();
        lastUnsavedStates.clear();
        for (const auto& tab : m_tabs) {
            lastUnsavedStates.push_back(tab.hasUnsavedChanges);
        }
        
        ImGui::EndTabBar();
    }
    
    // Store the actual height of the tab bar window for dockspace calculation
    m_tabBarHeight = ImGui::GetWindowHeight();
    
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace Propulsion
