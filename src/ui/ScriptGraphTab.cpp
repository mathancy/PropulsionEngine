// Standard headers first: some engine headers pull in <windows.h>, whose min/max macros break them.
#include <algorithm>
#include <filesystem>

#include "Engine.h"
#include "scripting/ScriptGraphEditor.h"

namespace Forge {

void Engine::openScriptGraphTab(const std::string& scriptPath) {
    if (m_currentProjectPath.empty()) {
        m_consoleBuffer.push_back("Save the project first; scripts live in the project's Scripts folder.");
        return;
    }
    std::filesystem::path full = std::filesystem::path(m_currentProjectPath).parent_path() / scriptPath;
    if (!std::filesystem::exists(full)) {
        m_consoleBuffer.push_back("There is no script at " + scriptPath);
        return;
    }

    // Already open? Show it.
    for (size_t t = 0; t < m_tabs.size(); t++) {
        if (m_tabs[t].type == TabType::ScriptGraph && m_tabs[t].assetId == scriptPath) {
            m_activeTabIndex = static_cast<int>(t);
            m_tabs[t].needsSelection = true;
            return;
        }
    }

    Tab tab(std::filesystem::path(scriptPath).filename().string(), TabType::ScriptGraph);
    tab.assetId = scriptPath;
    tab.scriptGraph = std::make_shared<ScriptGraphEditor>(
        full.generic_string(), [this](const std::string& line) { m_consoleBuffer.push_back(line); });
    m_tabs.push_back(tab);
    m_activeTabIndex = static_cast<int>(m_tabs.size() - 1);
    m_tabs.back().needsSelection = true;
}

void Engine::renderScriptGraphTab(Tab& tab) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float totalTopHeight = ImGui::GetFrameHeight() + m_tabBarHeight;
    float footerHeight = ImGui::GetFrameHeight();

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + totalTopHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - totalTopHeight - footerHeight));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    std::string windowId = "##ScriptGraphTab" + tab.assetId;
    ImGui::Begin(windowId.c_str(), nullptr, flags);

    // A project that was saved with this tab open has no editor yet: make one.
    if (!tab.scriptGraph && !m_currentProjectPath.empty()) {
        std::filesystem::path full = std::filesystem::path(m_currentProjectPath).parent_path() / tab.assetId;
        tab.scriptGraph = std::make_shared<ScriptGraphEditor>(
            full.generic_string(), [this](const std::string& line) { m_consoleBuffer.push_back(line); });
    }
    if (tab.scriptGraph) {
        // The Components panel shows what the Object asset using this script is made of (an Object
        // asset is the "class": it holds the mesh and material, and the script is its behavior).
        {
            std::vector<ScriptGraphEditor::ComponentInfo> components;
            std::string objectName;
            int sharedBy = 0;
            for (const ContentItem& item : m_contentItems) {
                if (item.type != "Object" || item.scriptFile != tab.assetId) continue;
                if (sharedBy++ > 0) continue;  // the first one is shown; the count says there are more
                objectName = item.name;
                std::string meshName = "None", meshFile = "-", materialName = "None", materialFile = "-";
                const ContentItem* material = nullptr;
                for (const ContentItem& other : m_contentItems) {
                    if (other.type == "Mesh" && !item.meshReferenceGUID.empty() && other.id == item.meshReferenceGUID) {
                        meshName = other.name;
                        if (!other.filepath.empty()) meshFile = other.filepath;
                    }
                    if (other.type == "Material" && !item.materialReferenceGUID.empty() && other.id == item.materialReferenceGUID) {
                        materialName = other.name;
                        if (!other.filepath.empty()) materialFile = other.filepath;
                        material = &other;
                    }
                }

                // The Object draws one mesh. The material is what it is drawn with, and the Transform is
                // where the mesh sits measured from the Object's anchor (the point it is placed by), so
                // both belong to the Mesh.
                ScriptGraphEditor::ComponentInfo mesh;
                mesh.name = "Mesh";
                mesh.detail = meshName;
                mesh.note = "The mesh this Object is drawn with. Change it in the Object's editor.";
                mesh.details = {{"Asset", meshName}, {"File", meshFile}, {"Material", materialName}};
                components.push_back(std::move(mesh));

                ScriptGraphEditor::ComponentInfo materialInfo;
                materialInfo.name = "Material";
                materialInfo.parent = "Mesh";
                materialInfo.detail = materialName;
                materialInfo.note = "The material on the mesh. Change it in the Object's editor.";
                materialInfo.details = {{"Asset", materialName}, {"File", materialFile}};
                if (material) {
                    auto channel = [](float f) { return static_cast<unsigned>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
                    materialInfo.swatch = IM_COL32(channel(material->materialColor.r), channel(material->materialColor.g), channel(material->materialColor.b), 255);
                }
                components.push_back(std::move(materialInfo));

                ScriptGraphEditor::ComponentInfo transform;
                transform.name = "Transform";
                transform.parent = "Mesh";
                transform.detail = "from anchor";
                transform.note = "Where the mesh sits, measured from the Object's anchor. Scripts move the whole Object, so this stays. "
                                 "Objects don't store an offset yet, so the mesh sits exactly on the anchor; it will be editable here once they do.";
                transform.details = {{"Relative to", "the Object's anchor"}, {"Location", "0, 0, 0"}, {"Rotation", "0, 0, 0"}, {"Scale", "1, 1, 1"}};
                components.push_back(std::move(transform));
            }
            tab.scriptGraph->setObject(objectName, std::move(components), sharedBy);
        }
        tab.scriptGraph->render();
    } else {
        ImGui::TextUnformatted("Save the project first, so this script can be found.");
    }

    ImGui::End();
}

} // namespace Forge
