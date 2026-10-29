/**
 * ObjectProperties.cpp
 * The properties of an Object asset, as widgets: its mesh, material, script and components (today, the Camera).
 *
 * The Object editor and the Graph view both draw these same functions, so there is one implementation of each
 * property. Each returns true when the user changed something; the caller decides how to keep it (the Object
 * editor marks its tab unsaved and waits for Ctrl+S, the Graph view saves straight away, as the graph does for
 * the script).
 */

// Standard headers first: some engine headers pull in <windows.h>, whose min/max macros break them.
#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "Engine.h"
#include "platform/FileDialogs.h"
#include "project/ObjectFile.h"
#include "scripting/PlayMode.h"
#include "ui/ImGuiTheme.h"

namespace Propulsion {

namespace {

/// A drop-down that lists Mesh or Material assets with their thumbnails and returns the index of the one picked
/// (or -2 for "None"), or -1 if nothing was picked this frame.
int pickAsset(const char* comboId, const std::string& shown, const std::vector<ContentItem>& items, const char* type,
              const std::string& currentGuid, bool noneSelected) {
    int picked = -1;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(comboId, shown.c_str())) {
        if (ImGui::Selectable("None", noneSelected)) picked = -2;
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].type != type) continue;
            const bool isSelected = currentGuid == items[i].id;
            if (items[i].thumbnailTexture != VK_NULL_HANDLE) {
                ImGui::Image((ImTextureID)items[i].thumbnailTexture,
                             ImVec2(PropulsionMKI::UI::scaled(32), PropulsionMKI::UI::scaled(32)), ImVec2(0, 0), ImVec2(1, 1));
                ImGui::SameLine();
            }
            if (ImGui::Selectable(items[i].name.c_str(), isSelected, 0, ImVec2(0, PropulsionMKI::UI::scaled(32)))) {
                picked = static_cast<int>(i);
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return picked;
}

/// The name of the asset a GUID (or, for older Objects, an index) refers to.
std::string nameOfAsset(const std::vector<ContentItem>& items, const char* type, const std::string& guid, int legacyIndex) {
    if (!guid.empty()) {
        for (const ContentItem& other : items) {
            if (other.type == type && other.id == guid) return other.name;
        }
    } else if (legacyIndex >= 0 && legacyIndex < static_cast<int>(items.size())) {
        return items[legacyIndex].name;
    }
    return "None";
}

} // namespace

bool Engine::drawObjectMeshProperty(ContentItem& item) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Mesh:");
    ImGui::SameLine();
    const std::string shown = nameOfAsset(m_contentItems, "Mesh", item.meshReferenceGUID, item.meshReference);
    const bool none = item.meshReferenceGUID.empty() && item.meshReference == -1;
    const int picked = pickAsset("##MeshCombo", shown, m_contentItems, "Mesh", item.meshReferenceGUID, none);
    if (picked == -1) return false;
    if (picked == -2) {
        item.meshReference = -1;
        item.meshReferenceGUID.clear();
    } else {
        item.meshReference = picked;
        item.meshReferenceGUID = m_contentItems[picked].id;
    }
    syncInstancesOf(item.id);  // instances that don't override the mesh follow it
    return true;
}

bool Engine::drawObjectMaterialProperty(ContentItem& item) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Material:");
    ImGui::SameLine();
    const std::string shown = nameOfAsset(m_contentItems, "Material", item.materialReferenceGUID, item.materialReference);
    const bool none = item.materialReferenceGUID.empty() && item.materialReference == -1;
    const int picked = pickAsset("##MaterialCombo", shown, m_contentItems, "Material", item.materialReferenceGUID, none);
    if (picked == -1) return false;
    if (picked == -2) {
        item.materialReference = -1;
        item.materialReferenceGUID.clear();
    } else {
        item.materialReference = picked;
        item.materialReferenceGUID = m_contentItems[picked].id;
    }
    syncInstancesOf(item.id);  // instances that don't override the material follow it
    return true;
}

bool Engine::drawObjectScriptProperty(ContentItem& item, bool inGraphView) {
    // The Object's behavior. Left on None, the object just exists and is rendered from its mesh, material and
    // transform; nothing runs for it. A script is a text file in the project's Scripts folder, read when Play
    // starts (no rebuild needed).
    bool changed = false;
    const std::filesystem::path projectDirPath = std::filesystem::path(m_currentProjectPath).parent_path();
    const std::string projectDir = projectDirPath.string();
    const bool projectSaved = !m_currentProjectPath.empty();

    std::error_code ec;
    const bool scriptExists = item.scriptFile.empty() || std::filesystem::exists(projectDirPath / item.scriptFile, ec);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Script:");
    ImGui::SameLine();

    if (inGraphView) {
        // In the Graph view the open graph IS the Object's script, so which script it is can't be swapped from here
        // (that would leave the graph showing one file and the Object running another).
        ImGui::TextUnformatted(item.scriptFile.empty() ? "None" : item.scriptFile.c_str());
    } else {
        std::string currentScriptName = item.scriptFile.empty() ? "None" : item.scriptFile;
        if (!scriptExists) currentScriptName += " (missing)";
        if (ImGui::BeginCombo("##ScriptCombo", currentScriptName.c_str())) {
            if (ImGui::Selectable("None", item.scriptFile.empty())) {
                item.scriptFile.clear();
                changed = true;
            }
            for (const std::string& path : PlayMode::scriptsIn(projectDir)) {
                const bool isSelected = item.scriptFile == path;
                if (ImGui::Selectable(path.c_str(), isSelected)) {
                    item.scriptFile = path;
                    changed = true;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (!projectSaved) ImGui::BeginDisabled();
        if (ImGui::Button("New Script")) {
            std::string error;
            std::string created = PlayMode::createScript(projectDir, item.name, error);
            if (created.empty()) {
                OutputLog::error(LogCategory::Script, error);
            } else {
                item.scriptFile = created;
                changed = true;
                OutputLog::info(LogCategory::Script, "Created script: " + created);
                openScriptGraphTab(created);
            }
        }
        if (!projectSaved) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            PropulsionMKI::UI::tooltip(projectSaved ? "Create a new script for this object and open it as a graph"
                                                  : "Save the project first, so there is a folder for the script");
        }
    }

    if (!item.scriptFile.empty() && scriptExists) {
        if (ImGui::Button("Open as Text")) {
            if (!PropulsionMKI::Platform::openWithDefaultApp((projectDirPath / item.scriptFile).string())) {
                OutputLog::error(LogCategory::Script, "Could not open " + item.scriptFile + ". Open it in any text editor.");
            }
        }
        if (!inGraphView) {
            ImGui::SameLine();
            if (ImGui::Button("Edit Graph")) openScriptGraphTab(item.scriptFile);
        }
        ImGui::SameLine();
        if (ImGui::Button("Check")) {
            std::string problems = PlayMode::checkScript((projectDirPath / item.scriptFile).generic_string());
            if (problems.empty()) {
                OutputLog::info(LogCategory::Compile, item.scriptFile + ": no problems");
            } else {
                std::istringstream lines(problems);
                std::string line;
                OutputLog::error(LogCategory::Compile, item.scriptFile + " has problems:");
                while (std::getline(lines, line)) {
                    if (!line.empty()) OutputLog::error(LogCategory::Compile, line);
                }
            }
        }
    }

    ImGui::PushTextWrapPos(0.0f);
    if (item.scriptFile.empty()) {
        ImGui::TextDisabled("No script: this object just exists and is rendered.");
    } else if (!scriptExists) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "That file isn't in the project, so nothing will run.");
    } else {
        ImGui::TextDisabled("Press Play to run %s. No rebuild needed.", item.scriptFile.c_str());
    }
    ImGui::PopTextWrapPos();
    return changed;
}

bool Engine::drawAddComponentButton(ContentItem& item) {
    bool changed = false;
    if (ImGui::SmallButton("+ Add Component")) ImGui::OpenPopup("AddComponentPopup");
    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (ImGui::MenuItem("Camera", nullptr, false, !item.camera.enabled)) {
            const SpringArmSettings arm = item.camera.arm;  // an arm added first stays
            item.camera = CameraSettings();
            item.camera.arm = arm;
            item.camera.enabled = true;
            changed = true;
        }
        if (ImGui::MenuItem("Spring Arm", nullptr, false, !item.camera.arm.enabled)) {
            item.camera.arm = SpringArmSettings();
            item.camera.arm.enabled = true;
            changed = true;
        }
        if (ImGui::MenuItem("Character Movement", nullptr, false, !item.collision.character.enabled)) {
            item.collision.character = CharacterMovementSettings();
            item.collision.character.enabled = true;
            fitCharacterToMesh(item);
            changed = true;
        }
        if (ImGui::MenuItem("Directional Light", nullptr, false, !item.light.enabled)) {
            item.light = LightSettings::directional();
            changed = true;
        }
        if (ImGui::MenuItem("Point Light", nullptr, false, !item.light.enabled)) {
            item.light = LightSettings::point();
            changed = true;
        }
        if (item.camera.enabled || item.camera.arm.enabled || item.collision.character.enabled || item.light.enabled) {
            ImGui::TextDisabled("(one of each per object)");
        }
        ImGui::EndPopup();
    }
    return changed;
}

void Engine::fitCharacterToMesh(ContentItem& item) const {
    // The capsule is as wide as the mesh (its narrower side) and as tall as it, so the mesh stands exactly on the ground.
    // With no mesh there is nothing to fit to, and the capsule keeps the sizes it has.
    const Mesh* mesh = meshByGUID(item.meshReferenceGUID);
    if (!mesh) return;
    const glm::vec3 half = (mesh->getBoundsMax() - mesh->getBoundsMin()) * 0.5f;
    CharacterMovementSettings& m = item.collision.character;
    m.radius = (std::max)(CharacterMovementSettings::kMinRadius, (std::min)(half.x, half.y));
    m.halfHeight = (std::max)(m.radius, half.z);
    m.sanitize();
}

bool Engine::drawObjectCollisionProperty(ContentItem& item) {
    bool changed = ImGui::Checkbox("Solid", &item.collision.solid);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A solid object stops characters: they stand on it and can't walk through it, in the shape of its mesh. "
                          "Turn it off for things to pass through, like a pickup. An object with no mesh has no shape.");
    }
    return changed;
}

bool Engine::drawObjectCharacterProperty(ContentItem& item) {
    CharacterMovementSettings& m = item.collision.character;
    if (!m.enabled) return false;

    bool changed = false;
    changed |= ImGui::DragFloat("Capsule Radius", &m.radius, 0.01f, CharacterMovementSettings::kMinRadius,
                                CharacterMovementSettings::kMaxSize, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Half the width of the capsule that stands in for the character's body.");
    changed |= ImGui::DragFloat("Capsule Half Height", &m.halfHeight, 0.01f, CharacterMovementSettings::kMinRadius,
                                CharacterMovementSettings::kMaxSize, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Half the height of the capsule, to the top of its rounded end. The capsule is centered on the object, "
                          "so its feet are this far below the object's origin.");
    }
    if (ImGui::SmallButton("Fit to Mesh")) {
        fitCharacterToMesh(item);
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make the capsule as wide and tall as the object's mesh.");
    changed |= ImGui::DragFloat("Gravity", &m.gravity, 0.1f, 0.0f, 1000.0f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How fast it speeds up while falling, in units per second per second. 0 floats.");
    changed |= ImGui::DragFloat("Jump Speed", &m.jumpSpeed, 0.1f, 0.0f, 1000.0f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How fast a jump starts out going up.");
    if (m.gravity > 0.0f) ImGui::TextDisabled("A jump rises about %.2f.", m.jumpSpeed * m.jumpSpeed / (2.0f * m.gravity));
    changed |= ImGui::DragFloat("Max Slope", &m.maxSlopeDegrees, 0.5f, 0.0f, 89.0f, "%.0f deg");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ground steeper than this is a wall: it can't be stood on or walked up.");
    // Which Input Action makes it jump: a Button action, read through the Object's Input Mapping.
    {
        const std::string shown = m.jumpAction.empty() ? "None" : m.jumpAction;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Jump Action");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##JumpAction", shown.c_str())) {
            if (ImGui::Selectable("None", m.jumpAction.empty())) {
                m.jumpAction.clear();
                changed = true;
            }
            for (const ContentItem& other : m_contentItems) {
                if (other.type != "InputAction" || other.inputAction.type != InputValueType::Button) continue;
                if (ImGui::Selectable(other.name.c_str(), other.name == m.jumpAction)) {
                    m.jumpAction = other.name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The Input Action (a Button) that makes this character jump, through the Input Mapping this Object is played "
                              "with. None: it never jumps from input (for characters the player doesn't control).");
        }
        if (!m.jumpAction.empty() && item.inputMappingGUID.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "This Object has no Input Mapping, so nothing can make it jump.");
        }
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("The script says where to walk; the engine adds gravity and jumping, and stops the capsule at solid objects. "
                        "The capsule scales with the object.");
    ImGui::PopTextWrapPos();
    if (changed) m.sanitize();

    if (ImGui::Button("Remove Character Movement")) {
        m = CharacterMovementSettings();
        changed = true;
    }
    return changed;
}

bool Engine::drawObjectCameraProperty(ContentItem& item) {
    CameraSettings& cam = item.camera;
    if (!cam.enabled) return false;

    bool changed = false;
    changed |= ImGui::DragFloat("Field of View", &cam.fov, 0.5f, CameraSettings::kMinFov, CameraSettings::kMaxFov, "%.1f deg");
    changed |= ImGui::DragFloat("Near Clip", &cam.nearClip, 0.01f, 0.001f, 1000.0f, "%.3f");
    changed |= ImGui::DragFloat("Far Clip", &cam.farClip, 1.0f, 0.01f, 100000.0f, "%.1f");
    changed |= ImGui::Checkbox("Auto Activate for Player", &cam.autoActivate);
    changed |= ImGui::DragFloat3("Location", &cam.offset.x, 0.05f, 0.0f, 0.0f, "%.2f");
    changed |= ImGui::DragFloat3("Rotation", &cam.rotation.x, 0.5f, 0.0f, 0.0f, "%.1f");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(cam.arm.enabled
                            ? "Location and rotation are relative to the end of the Spring Arm. Forward is +Y, up is +Z."
                            : "Location and rotation are relative to the object. Forward is +Y, up is +Z.");
    ImGui::PopTextWrapPos();
    if (changed) cam.sanitize();

    if (ImGui::Button("Remove Camera")) {
        const SpringArmSettings arm = cam.arm;  // the arm is its own component and stays
        cam = CameraSettings();
        cam.arm = arm;
        changed = true;
    }
    return changed;
}

bool Engine::drawObjectLightProperty(ContentItem& item) {
    LightSettings& light = item.light;
    if (!light.enabled) return false;

    bool changed = false;
    const bool wasDirectional = light.type == LightSettings::Type::Directional;
    int type = wasDirectional ? 0 : 1;
    const char* types[] = {"Directional", "Point"};
    if (ImGui::Combo("Type", &type, types, 2)) {
        // A sun and a bulb mean very different amounts by "intensity": a light still at the old type's default
        // takes the new type's default, so switching doesn't leave a blinding (or invisible) light.
        const LightSettings was = wasDirectional ? LightSettings::directional() : LightSettings::point();
        light.type = type == 0 ? LightSettings::Type::Directional : LightSettings::Type::Point;
        if (light.intensity == was.intensity) {
            light.intensity = (type == 0 ? LightSettings::directional() : LightSettings::point()).intensity;
        }
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Directional: the sun. It shines the same everywhere, along the object's forward (+Y) direction, so rotate the "
                          "object to aim it. Point: a bulb. It shines in every direction from the object and fades with distance.");
    }
    changed |= ImGui::ColorEdit3("Color", &light.color.x);
    changed |= ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, LightSettings::kMaxIntensity, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(light.type == LightSettings::Type::Directional
                              ? "How bright the light is on a surface facing it. 3 is a bright sunny day."
                              : "How bright the light is one unit away. It falls off with the square of the distance.");
    }
    if (light.type == LightSettings::Type::Point) {
        changed |= ImGui::DragFloat("Range", &light.range, 0.1f, LightSettings::kMinRange, LightSettings::kMaxRange, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Past this distance the light has faded to nothing.");
        changed |= ImGui::DragFloat("Radius", &light.radius, 0.01f, 0.0f, LightSettings::kMaxRadius, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The size of the bulb. Bigger bulbs cast softer shadows in the ray-traced render modes.");
    } else {
        changed |= ImGui::DragFloat("Angular Diameter", &light.angularDiameterDeg, 0.01f, 0.0f, LightSettings::kMaxAngularDiameter, "%.2f deg");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How big the sun looks in the sky (the real one is about 0.53). Bigger suns cast softer shadows in the ray-traced render modes.");
    }
    changed |= ImGui::Checkbox("Cast Shadows", &light.castShadows);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shadows come with the ray-traced render modes; the plain renderer draws none.");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(light.type == LightSettings::Type::Directional
                            ? "Shines along the object's forward direction (+Y). Where the object stands doesn't matter."
                            : "Shines from the object's position.");
    ImGui::PopTextWrapPos();
    if (changed) light.sanitize();

    if (ImGui::Button("Remove Light")) {
        light = LightSettings();
        changed = true;
    }
    return changed;
}

bool Engine::drawObjectSpringArmProperty(ContentItem& item) {
    SpringArmSettings& arm = item.camera.arm;
    if (!arm.enabled) return false;

    bool changed = false;
    changed |= ImGui::DragFloat("Target Arm Length", &arm.targetArmLength, 0.05f, 0.0f, SpringArmSettings::kMaxLength, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How far behind its fixed point the camera hangs. 0 is a first-person camera: it sits at the "
                          "Target Offset and turns on the spot.");
    }
    changed |= ImGui::DragFloat3("Target Offset", &arm.targetOffset.x, 0.05f, 0.0f, 0.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Where the arm is fixed, relative to the object: the point the camera orbits and looks at. "
                          "Head or eye height, usually.");
    }
    changed |= ImGui::DragFloat3("Socket Offset", &arm.socketOffset.x, 0.05f, 0.0f, 0.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Moves the camera from the end of the arm, in the arm's own directions (+X is to the right). "
                          "Slide it to one side for an over-the-shoulder view.");
    }
    changed |= ImGui::Checkbox("Use Pawn Control Rotation", &arm.usePawnControlRotation);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("While playing, looking up and down (the object's rotation about X) swings only the arm. "
                          "The object stays upright and walks along the ground.");
    }

    // How the arm behaves while playing.
    changed |= ImGui::Checkbox("Do Collision Test", &arm.doCollisionTest);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The camera stops in front of solid objects instead of going through them: it pulls in when "
                          "a wall is behind it, and goes back out when the way is clear.");
    }
    if (arm.doCollisionTest) {
        changed |= ImGui::DragFloat("Probe Size", &arm.probeSize, 0.01f, 0.0f, 10.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How big a ball is slid along the arm to find what is in the way: the camera stays this far "
                              "from a wall. Keep it smaller than the character's radius, and bigger than the camera's near clip.");
        }
    }
    changed |= ImGui::Checkbox("Enable Camera Lag", &arm.enableCameraLag);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The point the arm is fixed at trails behind the object as it moves, and catches up when it stops.");
    }
    if (arm.enableCameraLag) {
        changed |= ImGui::DragFloat("Camera Lag Speed", &arm.cameraLagSpeed, 0.1f, 0.0f, 1000.0f, "%.1f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How quickly the camera catches up. Higher is tighter; 0 means no lag at all.");
        }
        changed |= ImGui::DragFloat("Camera Lag Max Distance", &arm.cameraLagMaxDistance, 0.05f, 0.0f, 1000.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The furthest the camera may trail behind. 0 means no limit.");
        }
    }
    changed |= ImGui::Checkbox("Enable Camera Rotation Lag", &arm.enableCameraRotationLag);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The arm turns after where the player is looking, instead of turning with it.");
    }
    if (arm.enableCameraRotationLag) {
        changed |= ImGui::DragFloat("Camera Rotation Lag Speed", &arm.cameraRotationLagSpeed, 0.1f, 0.0f, 1000.0f, "%.1f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How quickly the arm catches up with where the player is looking. Higher is tighter; 0 means no lag at all.");
        }
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(item.camera.enabled ? "The Camera hangs from the end of this arm."
                                            : "Add a Camera for the arm to hold. Forward is +Y, up is +Z.");
    ImGui::PopTextWrapPos();
    if (changed) arm.sanitize();

    if (ImGui::Button("Remove Spring Arm")) {
        arm = SpringArmSettings();
        changed = true;
    }
    return changed;
}

bool Engine::drawObjectComponents(ContentItem& item) {
    // The Object editor's version: a list with each component's settings inline. (The Graph view shows the same
    // widgets in its Components and Details panels.)
    bool changed = false;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Components");
    ImGui::SameLine();
    changed |= drawAddComponentButton(item);

    if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= drawObjectCollisionProperty(item);
    }
    if (item.collision.character.enabled && ImGui::CollapsingHeader("Character Movement", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= drawObjectCharacterProperty(item);
    }
    if (item.camera.arm.enabled && ImGui::CollapsingHeader("Spring Arm", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= drawObjectSpringArmProperty(item);
    }
    if (item.camera.enabled && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= drawObjectCameraProperty(item);
    }
    if (item.light.enabled && ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= drawObjectLightProperty(item);
    }
    return changed;
}

bool Engine::saveObjectAsset(ContentItem& item) {
    if (m_currentProjectPath.empty() || item.filepath.empty()) return false;  // not in a saved project yet
    bool saved = false;
    try {
        CrashHandler::logInfo("Saving object: " + item.name);
        const std::filesystem::path objectFile = std::filesystem::path(m_currentProjectPath).parent_path() / item.filepath;
        std::filesystem::create_directories(objectFile.parent_path());

        std::ofstream out(objectFile, std::ios::binary);
        if (out.is_open()) {
            ObjectFile::write(out, ObjectFile::dataOf(item));
            out.close();
            saved = true;
            CrashHandler::logInfo("Object saved successfully (v" + std::to_string(ObjectFile::kVersion) + "): " + objectFile.string());
        } else {
            CrashHandler::logError("Failed to open object file for writing: " + objectFile.string());
        }
    } catch (const std::exception& e) {
        CrashHandler::logError("Exception saving object: " + std::string(e.what()));
    }

    // An Object editor tab for this asset has nothing left unsaved (it shows the same item).
    if (saved) {
        for (Tab& tab : m_tabs) {
            if (tab.type == TabType::AssetEditor && tab.contentItemIndex >= 0 &&
                tab.contentItemIndex < static_cast<int>(m_contentItems.size()) && &m_contentItems[tab.contentItemIndex] == &item) {
                tab.hasUnsavedChanges = false;
            }
        }
    }
    return saved;
}

void Engine::retargetMeshReferences(const std::string& oldId, const std::string& newId) {
    if (oldId == newId) return;
    // Objects (saved to their files), placed instances, and any tab showing the mesh all name it by ID.
    for (ContentItem& other : m_contentItems) {
        if (other.type == "Object" && other.meshReferenceGUID == oldId) {
            other.meshReferenceGUID = newId;
            saveObjectAsset(other);
        }
    }
    for (SceneObject& obj : m_sceneObjects) {
        if (obj.meshGUID == oldId) obj.meshGUID = newId;
    }
    for (Tab& tab : m_tabs) {
        if (tab.type == TabType::AssetEditor && tab.assetId == oldId) tab.assetId = newId;
    }
    markProjectModified();
}

} // namespace Propulsion
