// Standard headers first: some engine headers pull in <windows.h>, whose min/max macros break them.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>

#include "Engine.h"
#include "scripting/ScriptGraphEditor.h"

namespace Propulsion {

void Engine::openScriptGraphTab(const std::string& scriptPath) {
    if (m_currentProjectPath.empty()) {
        OutputLog::warning(LogCategory::ScriptGraph, "Save the project first; scripts live in the project's Scripts folder.");
        return;
    }
    std::filesystem::path full = std::filesystem::path(m_currentProjectPath).parent_path() / scriptPath;
    if (!std::filesystem::exists(full)) {
        OutputLog::warning(LogCategory::ScriptGraph, "There is no script at " + scriptPath);
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
        full.generic_string(), &OutputLog::write);
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
            full.generic_string(), &OutputLog::write);
    }
    if (tab.scriptGraph) {
        // The Components panel shows what the Object asset using this script is made of (an Object
        // asset is the "class": it holds the mesh and material, and the script is its behavior).
        {
            std::vector<ScriptGraphEditor::ComponentInfo> components;
            std::string objectName;
            int sharedBy = 0;
            std::function<void()> objectEditor, addComponent;
            for (size_t index = 0; index < m_contentItems.size(); index++) {
                const ContentItem& item = m_contentItems[index];
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

                // Every property of the Object asset is editable here, with the same widgets as the Object editor
                // (ObjectProperties.cpp). A change is saved to the asset straight away, the way the graph saves
                // the script. The lambdas run during this frame's render, so the index is still good.
                auto edited = [this, index](bool (Engine::*property)(ContentItem&)) {
                    ContentItem& target = m_contentItems[index];
                    if ((this->*property)(target)) saveObjectAsset(target);
                };

                // The Object draws one mesh. The material is what it is drawn with, and the Transform is
                // where the mesh sits measured from the Object's anchor (the point it is placed by), so
                // both belong to the Mesh.
                ScriptGraphEditor::ComponentInfo mesh;
                mesh.name = "Mesh";
                mesh.detail = meshName;
                mesh.note = "The mesh this Object is drawn with.";
                mesh.details = {{"Asset", meshName}, {"File", meshFile}, {"Material", materialName}};
                mesh.editor = [this, edited, meshFile]() {
                    edited(&Engine::drawObjectMeshProperty);
                    ImGui::Spacing();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("File");
                    ImGui::SameLine();
                    ImGui::TextUnformatted(meshFile.c_str());
                };
                components.push_back(std::move(mesh));

                ScriptGraphEditor::ComponentInfo materialInfo;
                materialInfo.name = "Material";
                materialInfo.parent = "Mesh";
                materialInfo.detail = materialName;
                materialInfo.note = "The material on the mesh.";
                materialInfo.details = {{"Asset", materialName}, {"File", materialFile}};
                ImU32 swatch = 0;
                if (material) {
                    auto channel = [](float f) { return static_cast<unsigned>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
                    swatch = IM_COL32(channel(material->materialColor.r), channel(material->materialColor.g), channel(material->materialColor.b), 255);
                    materialInfo.swatch = swatch;
                }
                materialInfo.editor = [this, edited, swatch, materialFile]() {
                    edited(&Engine::drawObjectMaterialProperty);
                    ImGui::Spacing();
                    if (swatch != 0) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Base color");
                        ImGui::SameLine();
                        ImGui::ColorButton("##swatch", ImGui::ColorConvertU32ToFloat4(swatch), ImGuiColorEditFlags_NoAlpha,
                                           ImVec2(ImGui::GetFontSize() * 2.5f, ImGui::GetFrameHeight()));
                    }
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("File");
                    ImGui::SameLine();
                    ImGui::TextUnformatted(materialFile.c_str());
                };
                components.push_back(std::move(materialInfo));

                ScriptGraphEditor::ComponentInfo transform;
                transform.name = "Transform";
                transform.parent = "Mesh";
                transform.detail = "from anchor";
                transform.note = "Where the mesh sits, measured from the Object's anchor. Scripts move the whole Object, so this stays. "
                                 "Objects don't store an offset yet, so the mesh sits exactly on the anchor; it will be editable here once they do.";
                transform.details = {{"Relative to", "the Object's anchor"}, {"Location", "0, 0, 0"}, {"Rotation", "0, 0, 0"}, {"Scale", "1, 1, 1"}};
                components.push_back(std::move(transform));

                ScriptGraphEditor::ComponentInfo collision;
                collision.name = "Collision";
                collision.parent = "Mesh";
                collision.detail = item.collision.solid ? "solid" : "pass-through";
                collision.note = "Whether characters can walk through this Object. Its shape is its mesh's.";
                collision.editor = [edited]() { edited(&Engine::drawObjectCollisionProperty); };
                components.push_back(std::move(collision));

                if (item.collision.character.enabled) {
                    ScriptGraphEditor::ComponentInfo character;
                    character.name = "Character Movement";
                    char tall[32];
                    std::snprintf(tall, sizeof(tall), "%.1f tall", item.collision.character.halfHeight * 2.0f);
                    character.detail = tall;
                    character.note = "Gives the Object a capsule that stands on the ground, falls, jumps, and stops at solid objects. "
                                     "The script says where to walk.";
                    character.editor = [edited]() { edited(&Engine::drawObjectCharacterProperty); };
                    components.push_back(std::move(character));
                }

                if (item.camera.arm.enabled) {
                    ScriptGraphEditor::ComponentInfo arm;
                    arm.name = "Spring Arm";
                    char length[32];
                    std::snprintf(length, sizeof(length), "%.1f long", item.camera.arm.targetArmLength);
                    arm.detail = length;
                    arm.note = "A boom that holds the Camera at a distance behind the Object. Looking up and down swings it around the Object, "
                               "which stays upright.";
                    arm.editor = [edited]() { edited(&Engine::drawObjectSpringArmProperty); };
                    components.push_back(std::move(arm));
                }

                if (item.camera.enabled) {
                    ScriptGraphEditor::ComponentInfo camera;
                    camera.name = "Camera";
                    if (item.camera.arm.enabled) camera.parent = "Spring Arm";
                    char fov[32];
                    std::snprintf(fov, sizeof(fov), "FOV %.0f", item.camera.fov);
                    camera.detail = fov;
                    camera.note = "A view into the world. While Play runs, the camera set to Auto Activate for Player is what you look through.";
                    camera.editor = [edited]() { edited(&Engine::drawObjectCameraProperty); };
                    components.push_back(std::move(camera));
                }

                if (item.light.enabled) {
                    ScriptGraphEditor::ComponentInfo light;
                    light.name = "Light";
                    light.detail = item.light.type == LightSettings::Type::Directional ? "directional" : "point";
                    light.note = "Makes the Object shine. A directional light is the sun and shines along the Object's forward direction; "
                                 "a point light is a bulb.";
                    light.editor = [edited]() { edited(&Engine::drawObjectLightProperty); };
                    components.push_back(std::move(light));
                }

                objectEditor = [this, index, edited]() {
                    drawObjectScriptProperty(m_contentItems[index], true);
                    ImGui::Spacing();
                    edited(&Engine::drawObjectInputProperty);
                };
                addComponent = [edited]() { edited(&Engine::drawAddComponentButton); };
            }
            tab.scriptGraph->setObject(objectName, std::move(components), sharedBy, std::move(objectEditor), std::move(addComponent));
        }
        tab.scriptGraph->render();
    } else {
        ImGui::TextUnformatted("Save the project first, so this script can be found.");
    }

    ImGui::End();
}

} // namespace Propulsion
