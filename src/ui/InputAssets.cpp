/**
 * InputAssets.cpp
 * Input as assets, modeled on Unreal's Enhanced Input:
 *   Input Action (.pinputaction)   one file per action; its name is what scripts call it; a Button or an Axis
 *   Input Mapping (.pinputmap)     one table of `action <- key` rows (with Negate and Scale) that an Object is played with
 * This file makes them (and the defaults a project starts with), saves them, and draws their editor tabs and the
 * Object's "Input Mapping" picker. What they mean at run time is in input/InputMap.h.
 */

// Standard headers first: some engine headers pull in <windows.h>, whose min/max macros break them.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "Engine.h"
#include "input/DefaultInput.h"
#include "input/InputKeys.h"
#include "project/InputFiles.h"
#include "scripting/PlayMode.h"
#include "ui/ImGuiTheme.h"

namespace Propulsion {

namespace {

const char* const kInputFolder = "Input";

bool isInputType(const std::string& type) { return type == "InputAction" || type == "InputMapping"; }

const char* extensionOf(const std::string& type) { return type == "InputAction" ? ".pinputaction" : ".pinputmap"; }

/// The ImGui key that is the same physical key as one of ours, or ImGuiKey_None.
ImGuiKey imguiKeyFor(const InputKeys::KeyInfo& key) {
    if (key.kind != InputKeys::Kind::Key) return ImGuiKey_None;
    const std::string& n = key.name;
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (n[0] - 'A'));
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (n[0] - '0'));
    if (n.size() >= 2 && n[0] == 'F' && std::isdigit(static_cast<unsigned char>(n[1]))) return static_cast<ImGuiKey>(ImGuiKey_F1 + (std::stoi(n.substr(1)) - 1));
    static const std::pair<const char*, ImGuiKey> others[] = {
        {"Space", ImGuiKey_Space}, {"Enter", ImGuiKey_Enter}, {"Tab", ImGuiKey_Tab}, {"Backspace", ImGuiKey_Backspace},
        {"Up", ImGuiKey_UpArrow}, {"Down", ImGuiKey_DownArrow}, {"Left", ImGuiKey_LeftArrow}, {"Right", ImGuiKey_RightArrow},
        {"LeftShift", ImGuiKey_LeftShift}, {"RightShift", ImGuiKey_RightShift}, {"LeftCtrl", ImGuiKey_LeftCtrl},
        {"RightCtrl", ImGuiKey_RightCtrl}, {"LeftAlt", ImGuiKey_LeftAlt}, {"RightAlt", ImGuiKey_RightAlt},
        {"Insert", ImGuiKey_Insert}, {"Delete", ImGuiKey_Delete}, {"Home", ImGuiKey_Home}, {"End", ImGuiKey_End},
        {"PageUp", ImGuiKey_PageUp}, {"PageDown", ImGuiKey_PageDown}, {"Comma", ImGuiKey_Comma}, {"Period", ImGuiKey_Period},
        {"Slash", ImGuiKey_Slash}, {"Semicolon", ImGuiKey_Semicolon}, {"Apostrophe", ImGuiKey_Apostrophe},
        {"LeftBracket", ImGuiKey_LeftBracket}, {"RightBracket", ImGuiKey_RightBracket}, {"Backslash", ImGuiKey_Backslash},
        {"Minus", ImGuiKey_Minus}, {"Equals", ImGuiKey_Equal}, {"Tilde", ImGuiKey_GraveAccent},
    };
    for (const auto& [name, imgui] : others) {
        if (n == name) return imgui;
    }
    return ImGuiKey_None;
}

/// Which row of the open Input Mapping is waiting for a key press (-1: none).
int s_listeningRow = -1;
int s_listeningAsset = -1;

const char* typeName(InputValueType t) { return t == InputValueType::Axis ? "Axis" : "Button"; }

} // namespace

// ---- Making, saving and keeping track of Input assets ----

void Engine::syncScriptInputs() {
    // Scripts are checked against the project's Input Actions; the graph editor offers their names.
    const InputSetup input = inputSetup();
    PlayMode::setScriptInputs(input.actions);

    // And so are scripts checked outside the editor (propc, the VS Code language server): they read a plain list of the
    // actions from a file next to the scripts. It is only written when it changes.
    if (m_currentProjectPath.empty()) return;
    std::string text = "// The Input Actions of this project, written by PropulsionMKI. Edit them in the editor, not here.\n";
    for (const InputActionDef& a : input.actions) text += std::string(a.type == InputValueType::Axis ? "axis " : "button ") + a.name + "\n";
    try {
        const std::filesystem::path dir = std::filesystem::path(m_currentProjectPath).parent_path() / "Scripts";
        const std::filesystem::path file = dir / "inputs.propapi";
        std::string existing;
        if (std::ifstream in{file, std::ios::binary}) existing.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (existing != text) {
            std::filesystem::create_directories(dir);
            std::ofstream(file, std::ios::binary) << text;
        }
    } catch (const std::exception& e) {
        CrashHandler::logWarning(std::string("Could not write Scripts/inputs.propapi: ") + e.what());
    }
}

bool Engine::saveInputAsset(ContentItem& item) {
    if (m_currentProjectPath.empty() || item.filepath.empty() || !isInputType(item.type)) return false;
    try {
        const std::filesystem::path file = std::filesystem::path(m_currentProjectPath).parent_path() / std::filesystem::path(item.filepath).make_preferred();
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        if (!out.is_open()) return false;
        if (item.type == "InputAction") {
            InputActionDef a = item.inputAction;
            a.id = item.id;
            InputFiles::writeAction(out, a);
        } else {
            InputMappingDef m = item.inputMapping;
            m.id = item.id;
            InputFiles::writeMapping(out, m);
        }
        syncScriptInputs();
        return true;
    } catch (const std::exception& e) {
        CrashHandler::logError(std::string("Failed to save input asset: ") + e.what());
        return false;
    }
}

void Engine::createInputAsset(const std::string& type, const std::string& typedName) {
    if (m_currentProjectPath.empty()) {
        OutputLog::warning(LogCategory::Input, "Save the project first, so there is a folder for the input asset.");
        return;
    }
    // An Input Action's file name is the name scripts use, so it has to be one they accept.
    std::string name = type == "InputAction" ? actionNameFrom(typedName) : typedName;
    if (type == "InputAction" && name != typedName) OutputLog::info(LogCategory::Input, "Input Action names are used in scripts, so \"" + typedName + "\" became \"" + name + "\".");
    for (const ContentItem& other : m_contentItems) {
        if (other.type == type && other.name == name) {
            OutputLog::warning(LogCategory::Input, "There is already an " + std::string(type == "InputAction" ? "Input Action" : "Input Mapping") + " called " + name + ".");
            return;
        }
    }

    const std::string folder = kInputFolder;
    ContentItem item(name, type, folder, std::string("Assets\\") + folder + "\\" + name + extensionOf(type));
    item.id = generateGUID();
    if (type == "InputAction") {
        item.inputAction.id = item.id;
        item.inputAction.name = name;
    } else {
        item.inputMapping.id = item.id;
        item.inputMapping.name = name;
    }
    if (!saveInputAsset(item)) {
        OutputLog::error(LogCategory::Input, "Could not create " + name + ".");
        return;
    }
    if (std::find(m_folders.begin(), m_folders.end(), folder) == m_folders.end()) {
        m_folders.push_back(folder);
        std::sort(m_folders.begin(), m_folders.end());
    }
    m_currentFolder = folder;
    m_selectedFolderIndex = static_cast<int>(std::find(m_folders.begin(), m_folders.end(), folder) - m_folders.begin());
    m_contentItems.push_back(item);

    Tab tab(name, TabType::AssetEditor, static_cast<int>(m_contentItems.size() - 1));
    tab.assetId = item.id;
    m_tabs.push_back(tab);
    m_activeTabIndex = static_cast<int>(m_tabs.size() - 1);
    syncScriptInputs();
    OutputLog::info(LogCategory::Input, std::string("Created ") + (type == "InputAction" ? "Input Action: " : "Input Mapping: ") + name);
}

void Engine::ensureDefaultInput() {
    if (m_currentProjectPath.empty()) return;
    for (const ContentItem& item : m_contentItems) {
        if (item.type == "InputAction") return;  // the project has its own
    }

    const InputSetup defaults = makeDefaultInput([this]() { return generateGUID(); });
    const std::string folder = kInputFolder;
    std::string mappingId;
    for (const InputActionDef& a : defaults.actions) {
        ContentItem item(a.name, "InputAction", folder, std::string("Assets\\") + folder + "\\" + a.name + ".pinputaction");
        item.id = a.id;
        item.inputAction = a;
        if (saveInputAsset(item)) m_contentItems.push_back(item);
    }
    for (const InputMappingDef& m : defaults.mappings) {
        ContentItem item(m.name, "InputMapping", folder, std::string("Assets\\") + folder + "\\" + m.name + ".pinputmap");
        item.id = m.id;
        item.inputMapping = m;
        if (saveInputAsset(item)) {
            m_contentItems.push_back(item);
            mappingId = item.id;
        }
    }
    if (std::find(m_folders.begin(), m_folders.end(), folder) == m_folders.end()) {
        m_folders.push_back(folder);
        std::sort(m_folders.begin(), m_folders.end());
    }

    // A project from before input was data has scripted Objects that read the old keys: give each the default mapping, so
    // they keep working (a script reads input through the mapping of the Object it runs on).
    int given = 0;
    for (ContentItem& item : m_contentItems) {
        if (item.type == "Object" && !item.scriptFile.empty() && item.inputMappingGUID.empty() && !mappingId.empty()) {
            item.inputMappingGUID = mappingId;
            saveObjectAsset(item);
            ++given;
        }
    }
    syncScriptInputs();
    OutputLog::info(LogCategory::Input, "Created the default Input Actions and the " + std::string(kDefaultMappingName) + " mapping in Assets/" + folder +
                              (given > 0 ? "; " + std::to_string(given) + " scripted Object(s) now use it." : "."));
}

// ---- The Object's Input Mapping ----

bool Engine::drawObjectInputProperty(ContentItem& item) {
    bool changed = false;
    std::string shown = "None";
    bool missing = false;
    if (!item.inputMappingGUID.empty()) {
        missing = true;
        for (const ContentItem& other : m_contentItems) {
            if (other.type == "InputMapping" && other.id == item.inputMappingGUID) {
                shown = other.name;
                missing = false;
            }
        }
        if (missing) shown = "(missing)";
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Input Mapping:");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##InputMappingCombo", shown.c_str())) {
        if (ImGui::Selectable("None", item.inputMappingGUID.empty())) {
            item.inputMappingGUID.clear();
            changed = true;
        }
        for (const ContentItem& other : m_contentItems) {
            if (other.type != "InputMapping") continue;
            const bool selected = other.id == item.inputMappingGUID;
            if (ImGui::Selectable(other.name.c_str(), selected)) {
                item.inputMappingGUID = other.id;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The table of keys this Object is played with. Its script reads input (Input is pressing jump, "
                          "axis moveForward of Input) through this mapping. Make one with Create Input Mapping in the Content Browser.");
    }
    ImGui::PushTextWrapPos(0.0f);
    if (item.inputMappingGUID.empty()) {
        ImGui::TextDisabled(item.scriptFile.empty() ? "No input mapping: this Object takes no input." : "No input mapping: the script on this Object gets no input.");
    } else if (missing) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "That Input Mapping isn't in the project any more, so there is no input.");
    }
    ImGui::PopTextWrapPos();
    return changed;
}

// ---- The editor tabs ----

namespace {

/// A combo of Input Actions: returns true and sets `actionId` if one was picked.
bool actionCombo(const char* id, const std::vector<ContentItem>& items, std::string& actionId) {
    std::string shown = "(missing action)";
    for (const ContentItem& it : items) {
        if (it.type == "InputAction" && it.id == actionId) shown = it.name + "  (" + typeName(it.inputAction.type) + ")";
    }
    bool picked = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, shown.c_str())) {
        for (const ContentItem& it : items) {
            if (it.type != "InputAction") continue;
            const std::string label = it.name + "  (" + typeName(it.inputAction.type) + ")";
            if (ImGui::Selectable(label.c_str(), it.id == actionId)) {
                actionId = it.id;
                picked = true;
            }
        }
        ImGui::EndCombo();
    }
    return picked;
}

} // namespace

void Engine::renderInputAssetEditorTab(Tab& tab) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float top = ImGui::GetFrameHeight() + m_tabBarHeight;
    const float footer = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + top));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - top - footer));
    ImGui::SetNextWindowViewport(viewport->ID);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    const std::string windowId = "##InputAsset" + std::to_string(tab.contentItemIndex);
    ImGui::Begin(windowId.c_str(), nullptr, flags);

    if (tab.contentItemIndex < 0 || tab.contentItemIndex >= static_cast<int>(m_contentItems.size())) {
        ImGui::TextDisabled("(No asset selected)");
        ImGui::End();
        return;
    }
    ContentItem& item = m_contentItems[tab.contentItemIndex];
    bool changed = false;
    const float gap = PropulsionMKI::UI::scaled(8.0f);

    if (item.type == "InputAction") {
        ImGui::Text("Input Action: %s", item.name.c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("A thing the player can do. Scripts use this file's name: rename it in the Content Browser (F2) and "
                            "scripts that use the old name stop compiling. Which key does it is set in an Input Mapping.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Value Type");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(PropulsionMKI::UI::scaled(160.0f));
        if (ImGui::BeginCombo("##ValueType", typeName(item.inputAction.type))) {
            for (InputValueType t : {InputValueType::Button, InputValueType::Axis}) {
                if (ImGui::Selectable(typeName(t), item.inputAction.type == t)) {
                    item.inputAction.type = t;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Button: down or up (jump, interact). Axis: a number, usually between -1 and 1 (walking, looking), "
                              "added up from every row of the mapping that feeds it.");
        }
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Description");
        char note[256];
        std::snprintf(note, sizeof(note), "%s", item.inputAction.description.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##Description", note, sizeof(note))) {
            item.inputAction.description = note;
            changed = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("In a script");
        if (item.inputAction.type == InputValueType::Button) {
            ImGui::TextDisabled("if Input is pressing %s, then ...", item.name.c_str());
            ImGui::TextDisabled("when Input starts pressing %s:", item.name.c_str());
            ImGui::TextDisabled("when Input stops pressing %s:", item.name.c_str());
        } else {
            ImGui::TextDisabled("remember Amount axis %s of Input", item.name.c_str());
        }
        ImGui::Spacing();
        ImGui::Text("Used by");
        int uses = 0;
        for (const ContentItem& other : m_contentItems) {
            if (other.type != "InputMapping") continue;
            int rows = 0;
            for (const InputRow& r : other.inputMapping.rows) rows += r.actionId == item.id ? 1 : 0;
            if (rows > 0) {
                ImGui::BulletText("%s: %d row(s)", other.name.c_str(), rows);
                ++uses;
            }
        }
        if (uses == 0) ImGui::TextDisabled("No Input Mapping uses it yet: nothing makes it happen.");
    } else {
        // ---- Input Mapping ----
        ImGui::Text("Input Mapping: %s", item.name.c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Which keys make which Input Actions happen. An Axis adds up its rows (W is +1 on moveForward, S is the same "
                            "with Negate). Pick the mapping on an Object, under Input Mapping, and its script is played with these keys.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        std::vector<InputRow>& rows = item.inputMapping.rows;
        int removeRow = -1;
        if (ImGui::BeginTable("##rows", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Negate", ImGuiTableColumnFlags_WidthFixed, PropulsionMKI::UI::scaled(60.0f));
            ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, PropulsionMKI::UI::scaled(90.0f));
            ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, PropulsionMKI::UI::scaled(30.0f));
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < rows.size(); ++i) {
                InputRow& row = rows[i];
                const ContentItem* action = nullptr;
                for (const ContentItem& other : m_contentItems) {
                    if (other.type == "InputAction" && other.id == row.actionId) action = &other;
                }
                const bool isButton = action && action->inputAction.type == InputValueType::Button;
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                if (actionCombo("##action", m_contentItems, row.actionId)) changed = true;

                ImGui::TableNextColumn();
                const bool listening = s_listeningAsset == tab.contentItemIndex && s_listeningRow == static_cast<int>(i);
                const float listenWidth = PropulsionMKI::UI::scaled(70.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - listenWidth - ImGui::GetStyle().ItemSpacing.x);
                const std::string keyShown = listening ? "Press a key..." : (row.key.empty() ? "(none)" : row.key);
                if (ImGui::BeginCombo("##key", keyShown.c_str())) {
                    for (const InputKeys::KeyInfo& key : InputKeys::all()) {
                        if (isButton && key.kind == InputKeys::Kind::MouseAxis) continue;  // a button can't be made by moving the mouse
                        if (ImGui::Selectable(key.name.c_str(), key.name == row.key)) {
                            row.key = key.name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::Button(listening ? "Cancel" : "Listen", ImVec2(listenWidth, 0))) {
                    s_listeningRow = listening ? -1 : static_cast<int>(i);
                    s_listeningAsset = tab.contentItemIndex;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click, then press the key you want. (Mouse buttons and movement are in the list.)");
                if (listening) {
                    for (const InputKeys::KeyInfo& key : InputKeys::all()) {
                        const ImGuiKey k = imguiKeyFor(key);
                        if (k != ImGuiKey_None && ImGui::IsKeyPressed(k, false)) {
                            row.key = key.name;
                            changed = true;
                            s_listeningRow = -1;
                        }
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) s_listeningRow = -1;
                }

                ImGui::TableNextColumn();
                if (isButton) ImGui::BeginDisabled();
                if (ImGui::Checkbox("##negate", &row.negate)) changed = true;
                if (isButton) ImGui::EndDisabled();

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat("##scale", &row.scale, 0.01f, -1000.0f, 1000.0f, "%.2f")) changed = true;

                ImGui::TableNextColumn();
                if (ImGui::Button("X")) removeRow = static_cast<int>(i);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this row");

                // Say so when a row can't do anything.
                if (!action) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "That Input Action isn't in the project any more.");
                } else if (!row.key.empty() && !InputKeys::find(row.key)) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "There is no key called %s.", row.key.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (removeRow >= 0) {
            rows.erase(rows.begin() + removeRow);
            s_listeningRow = -1;
            changed = true;
        }

        ImGui::Spacing();
        if (ImGui::Button("Add Row")) {
            InputRow row;
            for (const ContentItem& other : m_contentItems) {
                if (other.type == "InputAction") {
                    row.actionId = other.id;
                    break;
                }
            }
            rows.push_back(row);
            changed = true;
        }

        // The actions this mapping doesn't bind at all, with a quick way to add them.
        std::vector<const ContentItem*> unbound;
        for (const ContentItem& other : m_contentItems) {
            if (other.type != "InputAction") continue;
            bool used = false;
            for (const InputRow& r : rows) used = used || r.actionId == other.id;
            if (!used) unbound.push_back(&other);
        }
        if (!unbound.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Not bound in this mapping:");
            for (const ContentItem* a : unbound) {
                ImGui::SameLine(0.0f, gap);
                if (ImGui::SmallButton((a->name + " +").c_str())) {
                    InputRow row;
                    row.actionId = a->id;
                    rows.push_back(row);
                    changed = true;
                }
            }
        }
    }

    if (changed) tab.hasUnsavedChanges = true;

    ImGui::Spacing();
    ImGui::Separator();
    if (tab.hasUnsavedChanges) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "* Unsaved changes");
    if (ImGui::Button("Save") || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) {
        if (saveInputAsset(item)) {
            tab.hasUnsavedChanges = false;
            OutputLog::info(LogCategory::Input, std::string("Saved ") + (item.type == "InputAction" ? "Input Action: " : "Input Mapping: ") + item.name);
        } else {
            OutputLog::error(LogCategory::Input, "Could not save " + item.name + ".");
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+S");

    ImGui::End();
}

} // namespace Propulsion
