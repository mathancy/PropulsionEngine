/**
 * ScriptGraphPanels.cpp
 *
 * The panels around a script graph, laid out like Blueprints:
 *   - left, top: Components, what the Object this script belongs to is made of (the Mesh, and under it
 *     its Material and Transform);
 *   - left, middle: Variables, what the script declares in its header. A variable can be dragged onto a
 *     node's field (or onto the canvas, as a Get node to wire in), or picked from the field's dropdown;
 *   - left, bottom: References, the other entities and scripts the header says this script reaches for.
 *     They can be picked in the same dropdowns (an entity list is a list), and are added here;
 *   - right: Details, for whatever is selected: a variable (its type and default value), a reference, a
 *     component, or a node;
 *   - far right, when the Code button is on: the script's text, read-only, linked to the nodes;
 *   - under the graph: Compiler Results, after Compile.
 * Changing a variable here edits the script's header through the same edits as the graph does (see
 * propulsion_graph_edit), so it is undoable.
 *
 * Every size is a multiple of the current font height, so the panels follow File > Preferences >
 * UI Scaling (fonts and spacing are scaled by the theme; nothing here is in fixed pixels).
 */

// Standard headers first: some engine headers pull in <windows.h>, whose min/max macros break them.
#include "ui/ImGuiTheme.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "scripting/ScriptGraphEditor.h"

#include <imgui_internal.h>  // ImGui::GetActiveID: which text field is being typed in

namespace Propulsion {

namespace {

const char* const kBaseTypes[] = {"integer", "decimal", "boolean", "text", "vector2", "vector3", "color", "entity", "timer", "list"};
const char* const kElementTypes[] = {"integer", "decimal", "boolean", "text", "vector2", "vector3", "color", "entity"};

/// The colors Blueprints-style editors give to types, so a variable can be told at a glance.
ImU32 typeColor(const std::string& type) {
    std::string base = type;
    if (base.rfind("list of ", 0) == 0) base = base.substr(8);
    if (base == "integer") return IM_COL32(44, 214, 170, 255);
    if (base == "decimal") return IM_COL32(150, 235, 70, 255);
    if (base == "boolean") return IM_COL32(220, 60, 60, 255);
    if (base == "text") return IM_COL32(225, 80, 225, 255);
    if (base == "vector2" || base == "vector3") return IM_COL32(250, 200, 50, 255);
    if (base == "color") return IM_COL32(250, 150, 50, 255);
    if (base == "entity") return IM_COL32(70, 150, 255, 255);
    return IM_COL32(165, 165, 175, 255);
}

ImU32 componentColor(const std::string& name) {
    if (name == "@object") return IM_COL32(200, 200, 210, 255);
    if (name == "Transform") return IM_COL32(235, 150, 60, 255);
    if (name == "Mesh") return IM_COL32(80, 160, 235, 255);
    if (name == "Material") return IM_COL32(210, 95, 125, 255);
    if (name == "Camera") return IM_COL32(90, 165, 255, 255);
    return IM_COL32(160, 160, 170, 255);
}

/// What a new variable of this type holds to start with: the value text and the parts (x:0 y:0 ...).
void defaultsFor(const std::string& type, std::string& value, std::string& parts) {
    value.clear();
    parts.clear();
    if (type == "integer") value = "0";
    else if (type == "decimal") value = "0.0";
    else if (type == "boolean") value = "false";
    else if (type == "text") value = "\"\"";
    else if (type == "vector2") parts = "x:0 y:0";
    else if (type == "vector3") parts = "x:0 y:0 z:0";
    else if (type == "color") parts = "r:1.0 g:1.0 b:1.0 a:1.0";
}

std::string formatFloat(float f) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(f));
    std::string s = buf;
    while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
    return s;
}

/// "x:1 y:2" into key/value pairs, in order.
std::vector<std::pair<std::string, std::string>> splitParts(const std::string& parts) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t at = 0;
    while (at < parts.size()) {
        size_t end = parts.find(' ', at);
        if (end == std::string::npos) end = parts.size();
        std::string piece = parts.substr(at, end - at);
        size_t colon = piece.find(':');
        if (colon != std::string::npos) out.emplace_back(piece.substr(0, colon), piece.substr(colon + 1));
        at = end + 1;
    }
    return out;
}

bool isNumber(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && *end == '\0';
}

/// A selectable row: an icon, a name, and a note on the right. Everything is placed from the font height.
bool panelRow(const char* id, ImU32 iconColor, bool ring, const std::string& name, const std::string& note, bool selected, float indentEm) {
    const float em = ImGui::GetFontSize();
    const float rowHeight = ImGui::GetFrameHeight();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, rowHeight);
    const bool clicked = ImGui::Selectable(id, selected, ImGuiSelectableFlags_None, size);
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(lo, hi, true);
    const float cy = (lo.y + hi.y) * 0.5f;
    float x = lo.x + em * (0.3f + indentEm);
    const float r = em * 0.28f;
    if (ring) {
        dl->AddCircle(ImVec2(x + r, cy), r, iconColor, 0, (std::max)(1.5f, em * 0.12f));
    } else {
        dl->AddRectFilled(ImVec2(x, cy - r), ImVec2(x + 2 * r, cy + r), iconColor, em * 0.12f);
    }
    x += 2 * r + em * 0.45f;

    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(ImVec2(x, cy - ImGui::GetTextLineHeight() * 0.5f), textColor, name.c_str());
    if (!note.empty()) {
        const float noteWidth = ImGui::CalcTextSize(note.c_str()).x;
        const float nameWidth = ImGui::CalcTextSize(name.c_str()).x;
        const float noteX = hi.x - noteWidth - em * 0.4f;
        if (noteX > x + nameWidth + em * 0.6f) {
            dl->AddText(ImVec2(noteX, cy - ImGui::GetTextLineHeight() * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), note.c_str());
        }
    }
    dl->PopClipRect();
    return clicked;
}

void panelTitle(const char* title) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
}

/// "Label    value", the label dimmed and the value wrapped.
void detailRow(const char* label, const std::string& value) {
    const float labelWidth = ImGui::GetFontSize() * 5.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(labelWidth);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The layout
// ---------------------------------------------------------------------------------------------

void ScriptGraphEditor::drawPanels(const ImVec2& avail) {
    const float em = ImGui::GetFontSize();
    const float gap = (std::max)(4.0f, em * 0.35f);
    ImGuiIO& io = ImGui::GetIO();

    // Widths are kept in font heights, so a change of UI scale keeps the proportions. The graph in the
    // middle always keeps at least 12 of them.
    // With the code pane open there is a third line (between Details and the code) and a column more.
    if (!m_codeOpen) m_codeRows.clear();
    const float lines = m_codeOpen ? 3.0f : 2.0f;
    const float room = avail.x / em - 12.0f - lines * gap / em;
    const float codeMin = m_codeOpen ? 11.0f : 0.0f;
    m_sideWidthEm = std::clamp(m_sideWidthEm, 10.0f, (std::max)(10.0f, room - 11.0f - codeMin));
    m_detailsWidthEm = std::clamp(m_detailsWidthEm, 11.0f, (std::max)(11.0f, room - m_sideWidthEm - codeMin));
    if (m_codeOpen) m_codeWidthEm = std::clamp(m_codeWidthEm, codeMin, (std::max)(codeMin, room - m_sideWidthEm - m_detailsWidthEm));
    const float sideWidth = m_sideWidthEm * em;
    const float detailsWidth = m_detailsWidthEm * em;
    const float codeWidth = m_codeOpen ? m_codeWidthEm * em : 0.0f;

    // ---- left: Components over Variables over References ----
    ImGui::BeginChild("##ScriptSide", ImVec2(sideWidth, avail.y), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        const float height = ImGui::GetContentRegionAvail().y;
        const float minPanel = em * 4.0f;
        // Two lines between three panels; each keeps at least a few rows.
        const float usable = (std::max)(3.0f * minPanel, height - 2.0f * gap);
        const float topHeight = std::clamp(usable * m_componentsShare, minPanel, usable - 2.0f * minPanel);
        const float middleHeight = std::clamp(usable * m_variablesShare, minPanel, usable - topHeight - minPanel);

        ImGui::BeginChild("##Components", ImVec2(0, topHeight), ImGuiChildFlags_Borders);
        drawComponentsPanel();
        ImGui::EndChild();

        // Drag the lines between the panels.
        ImGui::InvisibleButton("##panelsplit", ImVec2(-FLT_MIN, gap));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive() && usable > 0.0f) {
            const float share = io.MouseDelta.y / usable;
            m_componentsShare = std::clamp(m_componentsShare + share, 0.08f, 0.70f);
            m_variablesShare = std::clamp(m_variablesShare - share, 0.12f, 0.80f);
        }

        ImGui::BeginChild("##Variables", ImVec2(0, middleHeight), ImGuiChildFlags_Borders);
        drawVariablesPanel();
        ImGui::EndChild();

        ImGui::InvisibleButton("##panelsplit2", ImVec2(-FLT_MIN, gap));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive() && usable > 0.0f) m_variablesShare = std::clamp(m_variablesShare + io.MouseDelta.y / usable, 0.12f, 0.80f);

        ImGui::BeginChild("##References", ImVec2(0, 0), ImGuiChildFlags_Borders);
        drawReferencesPanel();
        ImGui::EndChild();
    }
    ImGui::EndChild();

    // The line between the left panels and the graph.
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##sidesplit", ImVec2(gap, avail.y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 200, 100, 60));
    }
    if (ImGui::IsItemActive()) m_sideWidthEm += io.MouseDelta.x / em;
    ImGui::SameLine(0, 0);

    // ---- middle: the graph, and Compiler Results under it ----
    const float centerWidth = (std::max)(em * 4.0f, avail.x - sideWidth - detailsWidth - codeWidth - lines * gap);
    ImGui::BeginChild("##ScriptCenter", ImVec2(centerWidth, avail.y), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        const float resultsHeight = m_resultsOpen ? std::clamp(avail.y * 0.28f, em * 6.0f, em * 14.0f) : 0.0f;
        ImVec2 graphSize = ImGui::GetContentRegionAvail();
        graphSize.y = (std::max)(em * 4.0f, graphSize.y - (m_resultsOpen ? resultsHeight + gap : 0.0f));
        if (m_needsFit && m_loaded) {
            m_graph.fitView(graphSize);
            m_needsFit = false;
        }
        m_graph.render(graphSize);
        if (m_resultsOpen) drawCompileResults(resultsHeight);
    }
    ImGui::EndChild();

    // The line between the graph and Details.
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##detailsplit", ImVec2(gap, avail.y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 200, 100, 60));
    }
    if (ImGui::IsItemActive()) m_detailsWidthEm -= io.MouseDelta.x / em;
    ImGui::SameLine(0, 0);

    // ---- right: Details ----
    ImGui::BeginChild("##ScriptDetails", ImVec2(m_codeOpen ? detailsWidth : 0.0f, avail.y), ImGuiChildFlags_Borders);
    drawDetailsPanel();
    ImGui::EndChild();

    // ---- far right: the code ----
    if (m_codeOpen) {
        // The line between Details and the code moves width from one to the other; the graph stays as it is.
        ImGui::SameLine(0, 0);
        ImGui::InvisibleButton("##codesplit", ImVec2(gap, avail.y));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 200, 100, 60));
        }
        if (ImGui::IsItemActive()) {
            const float total = m_detailsWidthEm + m_codeWidthEm;
            m_detailsWidthEm = std::clamp(m_detailsWidthEm + io.MouseDelta.x / em, 11.0f, (std::max)(11.0f, total - codeMin));
            m_codeWidthEm = total - m_detailsWidthEm;
        }
        ImGui::SameLine(0, 0);
        ImGui::BeginChild("##ScriptCode", ImVec2(0, avail.y), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawCodePanel();
        ImGui::EndChild();
    }
}

// ---------------------------------------------------------------------------------------------
// Compile
// ---------------------------------------------------------------------------------------------

void ScriptGraphEditor::drawCompileStatus() {
    // The state: never compiled, edited since, failed, or fine.
    const bool never = !m_compile.done;
    const bool stale = m_compile.done && m_compile.stale;
    const bool failed = m_compile.done && !m_compile.stale && m_compile.errors > 0;
    ImVec4 tint = ImVec4(0.55f, 0.45f, 0.15f, 1.0f);                       // needs compiling
    std::string status = never ? "Not compiled" : "Changed since compiling";
    if (failed) {
        tint = ImVec4(0.70f, 0.20f, 0.20f, 1.0f);
        status = "Failed: " + std::to_string(m_compile.errors) + (m_compile.errors == 1 ? " error" : " errors");
    } else if (m_compile.done && !stale) {
        tint = ImVec4(0.18f, 0.55f, 0.28f, 1.0f);
        status = m_compile.warnings > 0 ? "Compiled, " + std::to_string(m_compile.warnings) + (m_compile.warnings == 1 ? " warning" : " warnings") : "Compiled";
    }
    (void)stale;

    ImGui::PushStyleColor(ImGuiCol_Button, tint);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(tint.x + 0.1f, tint.y + 0.1f, tint.z + 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(tint.x + 0.2f, tint.y + 0.2f, tint.z + 0.2f, 1.0f));
    if (ImGui::SmallButton("Compile")) m_pending.push_back([this]() { compile(); });
    ImGui::PopStyleColor(3);
    m_compileButton = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
    if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Check this script the way Play will, and list everything that would make it fail (F7)");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(tint.x + 0.35f, tint.y + 0.35f, tint.z + 0.35f, 1.0f), "%s", status.c_str());
}

void ScriptGraphEditor::selectNodeByKey(const std::string& key, View view) {
    ScriptNode* n = nodeByKey(key);
    if (!n) return;
    m_graph.selectNode(n->id);
    if (view == View::Center || (view == View::CenterIfHidden && !m_graph.isNodeInView(n->id))) m_graph.centerOnNode(n->id);
    m_selectedKey = n->key;
    m_focus = Focus::Node;
    m_selectedVar.clear();
    m_selectedRef.clear();
    m_selectedComponent.clear();
}

void ScriptGraphEditor::selectProblem(const Problem& p) {
    if (m_compile.stale) return;  // the script has changed: the node it names may not be that node any more
    if (!p.node.empty()) {
        selectNodeByKey(p.node, View::Center);
    } else if (!p.variable.empty()) {
        m_graph.clearSelection();
        m_selectedKey.clear();
        m_selectedComponent.clear();
        const bool isReference = std::any_of(m_refs.begin(), m_refs.end(), [&](const ReferenceInfo& r) { return r.name == p.variable; });
        m_selectedVar = isReference ? std::string() : p.variable;
        m_selectedRef = isReference ? p.variable : std::string();
        m_focus = isReference ? Focus::Reference : Focus::Variable;
    }
}

void ScriptGraphEditor::drawCompileResults(float height) {
    ImGui::BeginChild("##CompileResults", ImVec2(0, height), ImGuiChildFlags_Borders);
    panelTitle("COMPILER RESULTS");
    const float closeWidth = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - closeWidth);
    if (ImGui::SmallButton("Close")) m_resultsOpen = false;
    ImGui::Separator();

    if (m_compile.stale) {
        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f), "The script has changed since this compile. Compile again to refresh it.");
    } else if (m_compile.errors == 0) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "Compiled. Nothing here would stop Play.");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%d error(s): Play will not start until they are fixed.", m_compile.errors);
    }

    int index = 0;
    for (const Problem& p : m_compile.problems) {
        const std::string id = "##problem" + std::to_string(index++);
        if (panelRow(id.c_str(), p.error ? IM_COL32(235, 75, 75, 255) : IM_COL32(235, 190, 70, 255), false, p.message, "", false, 0.0f)) selectProblem(p);
        if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("%s\n(click to go to it)", p.message.c_str());
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------------------------

const ScriptGraphEditor::ComponentInfo* ScriptGraphEditor::componentNamed(const std::string& name) const {
    for (const ComponentInfo& c : m_components) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

void ScriptGraphEditor::selectComponent(const std::string& name) {
    m_selectedComponent = name;
    m_focus = Focus::Component;
    m_selectedVar.clear();
    m_selectedRef.clear();
    m_selectedKey.clear();
    m_graph.clearSelection();
}

void ScriptGraphEditor::drawComponentsPanel() {
    panelTitle("COMPONENTS");
    ImGui::Separator();

    if (m_objectName.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("No Object uses this script yet. Pick it as an Object's script to see what that Object is made of.");
        ImGui::PopTextWrapPos();
        return;
    }

    auto select = [&](const std::string& name) { selectComponent(name); };

    // The Object, then its Mesh, and under the Mesh its Material and its Transform (where the mesh sits,
    // measured from the Object's anchor).
    if (panelRow("##component_object", componentColor("@object"), false, m_objectName, "Object", m_selectedComponent == "@object", 0.0f)) select("@object");
    if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("The Object asset this script belongs to. Its anchor is where it is placed in a level.");

    for (const ComponentInfo& c : m_components) {
        if (!c.parent.empty()) continue;
        const std::string id = "##component_" + c.name;
        if (panelRow(id.c_str(), componentColor(c.name), false, c.name, c.detail, m_selectedComponent == c.name, 1.0f)) select(c.name);
        if (ImGui::IsItemHovered() && !c.note.empty()) PropulsionMKI::UI::tooltip("%s", c.note.c_str());
        for (const ComponentInfo& child : m_components) {
            if (child.parent != c.name) continue;
            const std::string childId = "##component_" + child.name;
            if (panelRow(childId.c_str(), componentColor(child.name), false, child.name, child.detail, m_selectedComponent == child.name, 2.0f)) select(child.name);
            if (ImGui::IsItemHovered() && !child.note.empty()) PropulsionMKI::UI::tooltip("%s", child.note.c_str());
        }
    }

    if (m_addComponent) {
        ImGui::Spacing();
        m_addComponent();
    }

    if (m_sharedBy > 1) {
        ImGui::Spacing();
        ImGui::TextDisabled("Shared by %d Objects", m_sharedBy);
    }
}

// ---------------------------------------------------------------------------------------------
// Variables
// ---------------------------------------------------------------------------------------------

const ScriptGraphEditor::VariableInfo* ScriptGraphEditor::selectedVariable() const {
    for (const VariableInfo& v : m_vars) {
        if (v.name == m_selectedVar) return &v;
    }
    return nullptr;
}

std::string ScriptGraphEditor::addVariable() {
    std::string name = "NewVariable";
    for (int n = 2; n < 200; n++) {
        bool taken = false;
        for (const VariableInfo& v : m_vars) taken = taken || v.name == name;
        if (!taken) break;
        name = "NewVariable" + std::to_string(n);
    }
    if (!edit("add_variable", "variable name:" + name + " type:decimal value:0.0")) return "";
    m_selectedVar = name;
    m_selectedRef.clear();
    m_selectedComponent.clear();
    m_focus = Focus::Variable;
    m_focusVarName = true;
    return name;
}

/// A text box that shows what the script says until it is clicked, then what is being typed, and
/// reports the typed text once it is left (or Enter is pressed). If the edit is then refused, the box
/// goes back to what the script says.
bool ScriptGraphEditor::textField(const char* id, const std::string& shown, std::string& typed) {
    std::string& buffer = m_fieldText[id];
    if (ImGui::GetActiveID() != ImGui::GetID(id)) buffer = shown;
    char text[256];
    std::strncpy(text, buffer.c_str(), sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText(id, text, sizeof(text), ImGuiInputTextFlags_AutoSelectAll)) buffer = text;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        typed = buffer;
        return true;
    }
    return false;
}

void ScriptGraphEditor::setVariableDecl(const VariableInfo& v, const std::string& type, const std::string& value, const std::string& parts, bool exposed) {
    std::string decl = v.kind + " name:" + v.name + " type:" + type;
    if (!value.empty()) decl += " value:" + value;
    if (!parts.empty()) decl += " " + parts;
    if (exposed && v.kind == "variable") decl += " exposed:true";
    const std::string name = v.name;
    m_pending.push_back([this, name, decl]() { edit("set_variable", name, decl); });
}

void ScriptGraphEditor::drawVariablesPanel() {
    panelTitle("VARIABLES");
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* addLabel = "+ Variable";
    const float addWidth = ImGui::CalcTextSize(addLabel).x + style.FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - addWidth);
    if (ImGui::Button(addLabel)) m_pending.push_back([this]() { addVariable(); });
    m_addButton = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
    if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Declare a new variable in this script's header");
    ImGui::Separator();

    if (m_vars.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("No variables yet. Add one, then pick it from a node's dropdown or drag it onto the node.");
        ImGui::PopTextWrapPos();
    }
    for (const VariableInfo& v : m_vars) {
        const std::string id = "##variable_" + v.name;
        const std::string note = v.kind == "constant" ? v.type + " (constant)" : v.type;
        if (panelRow(id.c_str(), typeColor(v.type), v.isList(), v.name, note, m_selectedVar == v.name && m_focus == Focus::Variable, 0.0f)) {
            m_selectedVar = v.name;
            m_selectedRef.clear();
            m_selectedComponent.clear();
            m_selectedKey.clear();
            m_graph.clearSelection();
            m_focus = Focus::Variable;
        }
        m_variableRows[v.name] = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
        // Drag it onto a node's field to plug it in, or onto the canvas for a Get node.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("PROPULSION_VARIABLE", v.name.c_str(), v.name.size() + 1);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(typeColor(v.type)), "%s", v.name.c_str());
            ImGui::TextDisabled("%s: drop it on a field", v.type.c_str());
            ImGui::EndDragDropSource();
        } else if (ImGui::IsItemHovered()) {
            PropulsionMKI::UI::tooltip("%s\nDrag it onto a node's field to plug it in.", v.text.c_str());
        }
    }

    // Local Variables: the names Set Local nodes make inside handlers and actions. They are not declared in the header
    // and only exist from their node to the end of its block, so they are listed apart, under the handler they are in.
    m_localRows.clear();
    ImGui::Spacing();
    char title[48];
    std::snprintf(title, sizeof(title), "Local Variables (%d)###locals", static_cast<int>(m_locals.size()));
    const bool open = ImGui::TreeNodeEx(title, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen);
    m_localsHeader = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
    if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Names made by a Set Local node (remember). Each one exists from its node to the end of the block it is in.");
    if (!open) return;
    if (m_locals.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("None yet. A Set Local node makes one.");
        ImGui::PopTextWrapPos();
    }
    // Several handlers: say which one each group belongs to.
    bool several = false;
    for (const LocalInfo& l : m_locals) several = several || l.scope != m_locals.front().scope;
    std::string scope;
    for (const LocalInfo& l : m_locals) {
        if (several && l.scope != scope) {
            scope = l.scope;
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", scope.c_str());
            ImGui::PopTextWrapPos();
        }
        const std::string id = "##local_" + l.name;
        const ImU32 color = l.type.empty() ? IM_COL32(160, 160, 170, 255) : typeColor(l.type);
        const bool isList = l.type.rfind("list of ", 0) == 0;
        const bool selected = m_focus == Focus::Node && m_selectedKey == l.node;
        if (panelRow(id.c_str(), color, isList, l.name, l.type.empty() ? "?" : l.type, selected, 0.6f)) {
            m_pending.push_back([this, node = l.node]() { selectNodeByKey(node, View::Center); });
        }
        m_localRows[l.name] = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
        // Like a variable, it can be dragged onto a field in the same handler (not onto the canvas: it isn't in the header).
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("PROPULSION_VARIABLE", l.name.c_str(), l.name.size() + 1);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s", l.name.c_str());
            ImGui::TextDisabled("%s: drop it on a field below its node", l.type.empty() ? "local" : l.type.c_str());
            ImGui::EndDragDropSource();
        } else if (ImGui::IsItemHovered()) {
            PropulsionMKI::UI::tooltip("%s\nMade by a Set Local node in %s (line %d).\nClick to find it; drag onto a field below it to use it.", l.name.c_str(), l.scope.c_str(), l.line);
        }
    }
    ImGui::TreePop();
}

// ---------------------------------------------------------------------------------------------
// References
// ---------------------------------------------------------------------------------------------

namespace {

ImU32 referenceColor(const std::string& kind) {
    if (kind == "file") return IM_COL32(170, 140, 235, 255);
    return typeColor("entity");
}

const char* referenceNote(const std::string& kind) {
    return kind == "entity" ? "entity" : kind == "entities" ? "list of entity" : "script";
}

} // namespace

const ScriptGraphEditor::ReferenceInfo* ScriptGraphEditor::selectedReference() const {
    for (const ReferenceInfo& r : m_refs) {
        if (r.name == m_selectedRef) return &r;
    }
    return nullptr;
}

void ScriptGraphEditor::drawReferencesPanel() {
    panelTitle("REFERENCES");
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* addLabel = "+ Reference";
    const float addWidth = ImGui::CalcTextSize(addLabel).x + style.FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - addWidth);
    if (ImGui::Button(addLabel)) {
        refreshScripts();
        ImGui::OpenPopup("##addreference");
    }
    m_addReferenceButton = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
    if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Reach for another entity or script from this one (goes in the header)");

    if (ImGui::BeginPopup("##addreference")) {
        if (ImGui::MenuItem("Entity", "one, found by tag")) m_pending.push_back([this]() { addReference("entity"); });
        if (ImGui::MenuItem("Entities", "every one with a tag")) m_pending.push_back([this]() { addReference("entities"); });
        ImGui::Separator();
        if (m_scripts.empty()) {
            ImGui::TextDisabled("No other scripts beside this one");
        } else if (ImGui::BeginMenu("Script")) {
            for (const std::string& file : m_scripts) {
                if (ImGui::MenuItem(file.c_str())) m_pending.push_back([this, file]() { addReference("file", file); });
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    if (m_refs.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Nothing yet. Add another entity, a list of entities, or a script this one talks to; entities can then be picked in a node's dropdown.");
        ImGui::PopTextWrapPos();
    }
    for (const ReferenceInfo& r : m_refs) {
        const std::string id = "##reference_" + r.name;
        if (panelRow(id.c_str(), referenceColor(r.kind), r.kind == "entities", r.name, referenceNote(r.kind), m_selectedRef == r.name && m_focus == Focus::Reference, 0.0f)) {
            m_selectedRef = r.name;
            m_selectedVar.clear();
            m_selectedComponent.clear();
            m_selectedKey.clear();
            m_graph.clearSelection();
            m_focus = Focus::Reference;
        }
        m_referenceRows[r.name] = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
        // An entity or a list of entities can be dragged onto a node's field like a variable.
        if (r.kind != "file" && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("PROPULSION_VARIABLE", r.name.c_str(), r.name.size() + 1);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(referenceColor(r.kind)), "%s", r.name.c_str());
            ImGui::TextDisabled("%s: drop it on a field", referenceNote(r.kind));
            ImGui::EndDragDropSource();
        } else if (ImGui::IsItemHovered()) {
            PropulsionMKI::UI::tooltip("%s", r.text.c_str());
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Details
// ---------------------------------------------------------------------------------------------

void ScriptGraphEditor::drawDetailsPanel() {
    panelTitle("DETAILS");
    ImGui::Separator();
    switch (m_focus) {
        case Focus::Variable:
            if (const VariableInfo* v = selectedVariable()) {
                drawVariableDetails(*v);
                return;
            }
            break;
        case Focus::Reference:
            if (const ReferenceInfo* r = selectedReference()) {
                drawReferenceDetails(*r);
                return;
            }
            break;
        case Focus::Component:
            drawComponentDetails();
            return;
        case Focus::Node:
            drawNodeDetails();
            return;
        case Focus::None:
            break;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Select a variable, a reference, a component, or a node to see its details.");
    ImGui::PopTextWrapPos();
}

void ScriptGraphEditor::drawComponentDetails() {
    const bool object = m_selectedComponent == "@object";
    const ComponentInfo* c = object ? nullptr : componentNamed(m_selectedComponent);
    if (!object && !c) {
        ImGui::TextDisabled("That component isn't on this Object any more.");
        return;
    }
    const std::string title = object ? m_objectName : c->name;
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(componentColor(object ? "@object" : c->name)), "%s", title.c_str());
    ImGui::TextDisabled("%s", object ? "Object" : (c->parent.empty() ? "Component" : ("Component of " + c->parent).c_str()));
    ImGui::Spacing();

    if (object) {
        if (m_objectEditor) {
            m_objectEditor();
        } else {
            detailRow("Script", std::filesystem::path(m_path).filename().string());
        }
        if (m_sharedBy > 1) detailRow("Shared by", std::to_string(m_sharedBy) + " Objects");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("The Object is what gets placed in a level. Select its components to edit its mesh, material and camera.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (c->editor) {
        ImGui::PushID(c->name.c_str());
        c->editor();
        ImGui::PopID();
        return;
    }
    for (const auto& row : c->details) detailRow(row.first.c_str(), row.second);
    if (c->swatch != 0) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Base color");
        ImGui::SameLine(ImGui::GetFontSize() * 5.0f);
        ImGui::ColorButton("##swatch", ImGui::ColorConvertU32ToFloat4(c->swatch), ImGuiColorEditFlags_NoAlpha, ImVec2(ImGui::GetFontSize() * 2.5f, ImGui::GetFrameHeight()));
    }
    if (!c->note.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", c->note.c_str());
        ImGui::PopTextWrapPos();
    }
}

void ScriptGraphEditor::drawNodeDetails() {
    ScriptNode* node = nodeByKey(m_selectedKey);
    if (!node) {
        ImGui::TextDisabled("That node isn't in the graph any more.");
        return;
    }
    ImGui::TextUnformatted(node->name.c_str());
    ImGui::TextDisabled("%s%s", node->nodeClass.c_str(), node->category.empty() ? "" : (" - " + node->category).c_str());
    ImGui::Spacing();

    if (node->isGetter) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Drag from the output pin onto a node's field to plug this variable in. The Get node goes away once it is wired.");
        ImGui::PopTextWrapPos();
        return;
    }
    for (const CatalogEntry& e : m_catalog) {
        if (e.name == node->name) {
            detailRow("Reads as", e.pattern);
            break;
        }
    }
    for (const ScriptPinField& f : node->fields) {
        if (f.kind == "flag") continue;
        detailRow(f.name.c_str(), f.text == "unset" ? std::string("(not plugged in)") : (f.text.empty() ? std::string("-") : f.text));
    }
    if (!node->comment.empty()) detailRow("Comment", node->comment);
    if (!node->error.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.0f));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(node->error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

void ScriptGraphEditor::drawVariableDetails(const VariableInfo& v) {
    const float em = ImGui::GetFontSize();
    const float labelWidth = em * 5.0f;
    auto label = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(labelWidth);
    };
    ImGui::PushID(v.name.c_str());

    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(typeColor(v.type)), "%s", v.name.c_str());
    ImGui::TextDisabled("%s", v.kind == "constant" ? (v.type + " constant").c_str() : (v.type + " variable").c_str());
    ImGui::Spacing();

    // Name: changing it renames every use in the script too.
    label("Name");
    if (m_focusVarName) {
        ImGui::SetKeyboardFocusHere();
        m_focusVarName = false;
    }
    std::string typed;
    if (textField("##varname", v.name, typed) && typed != v.name) {
        const std::string from = v.name, to = typed;
        m_pending.push_back([this, from, to]() {
            if (edit("rename_variable", from, to)) m_selectedVar = to;
        });
    }

    // Type: a base type, and for a list the type of what it holds. The enums and structs the script declares
    // (and `hit`) are types too; they are declared in the text, and listed here to pick.
    const bool isList = v.isList();
    std::string baseType = isList ? "list" : v.type;
    std::string element = isList ? v.type.substr(8) : std::string();
    label("Type");
    ImGui::SetNextItemWidth(isList ? -(em * 7.0f) : -FLT_MIN);
    std::string newType = v.type;
    bool typeChanged = false;
    if (ImGui::BeginCombo("##vartype", baseType.c_str())) {
        for (const char* t : kBaseTypes) {
            if (ImGui::Selectable(t, baseType == t) && baseType != t) {
                newType = std::string(t) == "list" ? "list of integer" : std::string(t);
                typeChanged = true;
            }
        }
        for (const TypeInfo& t : m_types) {
            if (ImGui::Selectable(t.name.c_str(), baseType == t.name) && baseType != t.name) {
                newType = t.name;
                typeChanged = true;
            }
        }
        ImGui::EndCombo();
    }
    if (isList) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool nested = element.rfind("list", 0) == 0;
        ImGui::BeginDisabled(nested);
        if (ImGui::BeginCombo("##varelement", element.c_str())) {
            for (const char* t : kElementTypes) {
                if (ImGui::Selectable(t, element == t) && element != t) {
                    newType = std::string("list of ") + t;
                    typeChanged = true;
                }
            }
            for (const TypeInfo& t : m_types) {
                if (ImGui::Selectable(t.name.c_str(), element == t.name) && element != t.name) {
                    newType = "list of " + t.name;
                    typeChanged = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
    }
    if (typeChanged) {
        // The old value means nothing as another type: start from that type's default.
        std::string value, parts;
        const std::string typeOnly = newType.rfind("list of ", 0) == 0 ? "list" : newType;
        defaultsFor(typeOnly, value, parts);
        setVariableDecl(v, newType, value, parts, v.exposed);
    }

    // Default value, shaped by the type.
    label("Default");
    if (v.type == "integer" || v.type == "decimal") {
        std::string text;
        if (textField("##varvalue", v.value, text)) {
            if (v.type == "decimal" && !text.empty() && text.find_first_of(".eE") == std::string::npos) {
                float f;
                if (isNumber(text, f)) text += ".0";
            }
            setVariableDecl(v, v.type, text, "", v.exposed);
        }
    } else if (v.type == "boolean") {
        bool on = v.value == "true";
        if (ImGui::Checkbox("##varvalue", &on)) setVariableDecl(v, v.type, on ? "true" : "false", "", v.exposed);
    } else if (v.type == "text") {
        std::string inner = v.value;
        if (inner.size() >= 2 && inner.front() == '"' && inner.back() == '"') inner = inner.substr(1, inner.size() - 2);
        std::string text;
        if (textField("##varvalue", inner, text)) {
            std::replace(text.begin(), text.end(), '"', '\'');
            setVariableDecl(v, v.type, "\"" + text + "\"", "", v.exposed);
        }
    } else if (v.type == "vector2" || v.type == "vector3" || v.type == "color") {
        std::vector<std::pair<std::string, std::string>> parts = splitParts(v.parts);
        std::string wanted = v.type == "vector2" ? "xy" : v.type == "vector3" ? "xyz" : "rgba";
        std::vector<std::pair<std::string, std::string>> shown;
        for (char key : wanted) {
            std::string value = key == 'a' ? "1.0" : "0";
            for (const auto& p : parts) {
                if (p.first == std::string(1, key)) value = p.second;
            }
            shown.emplace_back(std::string(1, key), value);
        }
        ImGui::NewLine();
        bool changed = false;
        bool numeric = true;
        float channel[4] = {0, 0, 0, 1};
        for (size_t i = 0; i < shown.size(); i++) {
            float f = 0.0f;
            if (isNumber(shown[i].second, f)) channel[i] = f;
            else numeric = false;
        }
        if (v.type == "color" && numeric) {
            label("");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::ColorEdit4("##varcolor", channel, ImGuiColorEditFlags_AlphaBar);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                for (size_t i = 0; i < shown.size(); i++) shown[i].second = formatFloat(channel[i]);
                changed = true;
            }
        }
        for (auto& p : shown) {
            label(("  " + p.first).c_str());
            std::string text;
            const std::string id = "##varpart_" + p.first;
            if (textField(id.c_str(), p.second, text)) {
                p.second = text;
                changed = true;
            }
        }
        if (changed) {
            std::string joined;
            for (const auto& p : shown) joined += (joined.empty() ? "" : " ") + p.first + ":" + p.second;
            setVariableDecl(v, v.type, "", joined, v.exposed);
        }
    } else if (const TypeInfo* declared = [&]() -> const TypeInfo* {
                   for (const TypeInfo& t : m_types) {
                       if (t.name == v.type) return &t;
                   }
                   return nullptr;
               }()) {
        if (declared->kind == "enum") {
            // One of the enum's values; leaving it out means the first.
            std::string current = v.value.empty() && !declared->values.empty() ? declared->values.front() : v.value;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##varvalue", current.c_str())) {
                for (const std::string& value : declared->values) {
                    if (ImGui::Selectable(value.c_str(), current == value) && current != value) setVariableDecl(v, v.type, value, "", v.exposed);
                }
                ImGui::EndCombo();
            }
        } else {
            std::string fields;
            for (const auto& f : declared->fields) fields += (fields.empty() ? "" : ", ") + f.first;
            ImGui::TextWrapped("A %s: starts with %s at their defaults", declared->name.c_str(), fields.c_str());
        }
    } else {
        ImGui::TextDisabled("Starts empty");
    }

    if (v.kind == "variable") {
        bool exposed = v.exposed;
        label("");
        if (ImGui::Checkbox("Exposed", &exposed)) setVariableDecl(v, v.type, v.value, v.parts, exposed);
        if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("An exposed variable's starting value may be overridden by each Object placed in a level.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Delete variable", ImVec2(-FLT_MIN, 0))) {
        const std::string name = v.name;
        m_pending.push_back([this, name]() {
            if (edit("delete_variable", name)) {
                m_selectedVar.clear();
                m_focus = Focus::None;
            }
        });
    }
    if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Remove it from the header. Nodes that used it will show a problem when you compile.");
    ImGui::PopID();
}

void ScriptGraphEditor::setReferenceDecl(const ReferenceInfo& r, const std::string& file, const std::string& tag, const std::string& when) {
    std::string decl;
    if (r.kind == "entity") {
        decl = "needs entity:" + r.name + " file:\"" + file + "\" tag:\"" + tag + "\"";
    } else if (r.kind == "entities") {
        decl = "needs entities:" + r.name + " tag:\"" + tag + "\"" + (file.empty() ? "" : " file:\"" + file + "\"");
    } else {
        decl = "needs file:\"" + file + "\" as " + r.name + (when.empty() ? "" : " when:" + when);
    }
    const std::string name = r.name;
    m_pending.push_back([this, name, decl]() { edit("set_reference", name, decl); });
}

void ScriptGraphEditor::drawReferenceDetails(const ReferenceInfo& r) {
    const float em = ImGui::GetFontSize();
    const float labelWidth = em * 5.0f;
    auto label = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(labelWidth);
    };
    ImGui::PushID(r.name.c_str());

    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(referenceColor(r.kind)), "%s", r.name.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", r.kind == "entity" ? "Another entity, found by its tag" : r.kind == "entities" ? "Every entity with a tag (a live list)" : "Another script this one talks to");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    label("Name");
    if (m_focusVarName) {
        ImGui::SetKeyboardFocusHere();
        m_focusVarName = false;
    }
    std::string typed;
    if (textField("##refname", r.name, typed) && typed != r.name) {
        const std::string from = r.name, to = typed;
        m_pending.push_back([this, from, to]() {
            if (edit("rename_reference", from, to)) m_selectedRef = to;
        });
    }

    // The script it names: a picker over the other scripts beside this one. An entity list may name none.
    label(r.kind == "file" ? "Script" : "Runs");
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool optionalFile = r.kind == "entities";
    const std::string shown = r.file.empty() ? std::string("(any script)") : r.file;
    if (ImGui::BeginCombo("##reffile", shown.c_str())) {
        if (optionalFile && ImGui::Selectable("(any script)", r.file.empty()) && !r.file.empty()) setReferenceDecl(r, "", r.tag, r.when);
        bool listed = false;
        for (const std::string& file : m_scripts) {
            listed = listed || file == r.file;
            if (ImGui::Selectable(file.c_str(), file == r.file) && file != r.file) setReferenceDecl(r, file, r.tag, r.when);
        }
        // A script that is not beside this one (or is missing) stays visible, so it can be seen and replaced.
        if (!listed && !r.file.empty()) ImGui::Selectable(r.file.c_str(), true);
        ImGui::EndCombo();
    }

    if (r.kind != "file") {
        label("Tag");
        std::string tag;
        if (textField("##reftag", r.tag, tag)) {
            std::replace(tag.begin(), tag.end(), '"', '\'');
            setReferenceDecl(r, r.file, tag, r.when);
        }
    } else {
        label("Loads");
        ImGui::SetNextItemWidth(-FLT_MIN);
        const std::string when = r.when.empty() ? std::string("used") : r.when;
        if (ImGui::BeginCombo("##refwhen", when.c_str())) {
            if (ImGui::Selectable("used", when == "used") && when != "used") setReferenceDecl(r, r.file, r.tag, "used");
            if (ImGui::Selectable("startup", when == "startup") && when != "startup") setReferenceDecl(r, r.file, r.tag, "startup");
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("used: loaded the first time it is needed\nstartup: loaded when this script is");
    }

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    if (r.kind == "file") {
        ImGui::TextDisabled("Use it with a Call Action node: do <Action> on %s.", r.name.c_str());
    } else {
        ImGui::TextDisabled("%s can be picked in a node's dropdown%s.", r.name.c_str(), r.kind == "entities" ? " (For Each, Count Of, Item Of, Remove From List)" : "");
    }
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    if (ImGui::Button("Delete reference", ImVec2(-FLT_MIN, 0))) {
        const std::string name = r.name;
        m_pending.push_back([this, name]() {
            if (edit("delete_reference", name)) {
                m_selectedRef.clear();
                m_focus = Focus::None;
            }
        });
    }
    if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Remove it from the header. Nodes that used it will show a problem when you compile.");
    ImGui::PopID();
}

// ---------------------------------------------------------------------------------------------
// Code
// ---------------------------------------------------------------------------------------------
//
// The script's text beside the graph. It is read-only: it shows the file as it is on disk, which the
// graph edits keep valid, so it never disagrees with the graph. PropulsionScript has few kinds of word, so
// the colors only say what a word is for (a section, an event, a verb, a variable...).

namespace {

constexpr double kCodeApplyDelay = 0.5;  // seconds typing has to pause before text that parses is saved

struct CodePalette {
    ImU32 text, comment, string, number, key, event, decl, control, verb, constant;
};

CodePalette codePalette() {
    CodePalette p;
    p.text = ImGui::GetColorU32(ImGuiCol_Text);
    p.comment = IM_COL32(106, 153, 85, 255);
    p.string = IM_COL32(206, 145, 120, 255);
    p.number = IM_COL32(181, 206, 168, 255);
    p.key = IM_COL32(156, 220, 254, 255);
    p.event = IM_COL32(86, 156, 214, 255);
    p.decl = IM_COL32(197, 134, 192, 255);
    p.control = IM_COL32(232, 124, 170, 255);
    p.verb = IM_COL32(220, 220, 170, 255);
    p.constant = IM_COL32(86, 156, 214, 255);
    return p;
}

/// `a` with some of `b` mixed in, so the vivid type colors sit quietly inside a line of text.
ImU32 mixColor(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, 1.0f));
}

struct CodeSpan {
    size_t begin, end;  // in the line's text
    ImU32 color;
};

bool isWordStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

/// The words that open a block or a branch; any other first word of a statement is a verb.
bool isControlWord(const std::string& w) {
    static const char* const words[] = {"if", "otherwise", "for", "while", "repeat", "after", "only", "first", "alternate", "through", "choose", "case", "animate", "then"};
    for (const char* c : words) {
        if (w == c) return true;
    }
    return false;
}

/// Split one line of the script into colored spans, in order; what is between them is plain text.
/// `section` is 1 inside `header:` and 2 inside `implement:`; `names` colors the words that are declared.
void highlightLine(const std::string& line, int section, const CodePalette& pal, const std::map<std::string, ImU32>& names, std::vector<CodeSpan>& out) {
    out.clear();
    size_t tabs = 0;
    while (tabs < line.size() && line[tabs] == '\t') tabs++;

    // Where a trailing comment starts (a `//` inside a text is not one).
    size_t codeEnd = line.size();
    size_t commentAt = std::string::npos;
    bool inText = false;
    for (size_t i = tabs; i < line.size(); i++) {
        if (line[i] == '"') inText = !inText;
        else if (!inText && line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            commentAt = i;
            codeEnd = i;
            break;
        }
    }
    while (codeEnd > tabs && (line[codeEnd - 1] == ' ' || line[codeEnd - 1] == '\t' || line[codeEnd - 1] == '\r')) codeEnd--;

    if (codeEnd > tabs) {
        const bool endsWithColon = line[codeEnd - 1] == ':';
        const bool declLine = section == 1 && tabs >= 1;
        const bool eventLine = section == 2 && tabs == 1 && endsWithColon;
        const bool statementLine = section == 2 && tabs >= 1 && !eventLine;

        if (tabs == 0) {
            out.push_back({tabs, codeEnd, pal.decl});  // header: / implement:
        } else {
            bool first = true;
            size_t i = tabs;
            while (i < codeEnd) {
                const char c = line[i];
                if (c == '"') {
                    size_t j = i + 1;
                    while (j < codeEnd && line[j] != '"') j++;
                    if (j < codeEnd) j++;
                    out.push_back({i, j, pal.string});
                    i = j;
                    first = false;
                } else if ((isDigit(c) || (c == '-' && i + 1 < codeEnd && isDigit(line[i + 1]))) && (i == tabs || !isWordChar(line[i - 1]))) {
                    size_t j = i + 1;
                    while (j < codeEnd && (isDigit(line[j]) || (line[j] == '.' && j + 1 < codeEnd && isDigit(line[j + 1])))) j++;
                    out.push_back({i, j, pal.number});
                    i = j;
                    first = false;
                } else if (isWordStart(c)) {
                    size_t j = i + 1;
                    while (j < codeEnd && isWordChar(line[j])) j++;
                    const std::string word = line.substr(i, j - i);
                    // `name:Speed` is a key; a colon at the end of the line is not.
                    const bool isKey = j + 1 < codeEnd && line[j] == ':' && line[j + 1] != ' ' && line[j + 1] != '\t';
                    ImU32 color = 0;
                    if (isKey) {
                        color = pal.key;
                        j++;
                    } else if (declLine && first) {
                        color = pal.decl;
                    } else if (eventLine) {
                        color = pal.event;
                    } else if (statementLine && first) {
                        color = (endsWithColon || isControlWord(word)) ? pal.control : pal.verb;
                    } else if (word == "true" || word == "false") {
                        color = pal.constant;
                    } else if (isControlWord(word)) {
                        color = pal.control;
                    } else {
                        auto named = names.find(word);
                        if (named != names.end()) color = named->second;
                    }
                    if (color != 0) out.push_back({i, j, color});
                    i = j;
                    first = false;
                } else {
                    i++;  // spaces and punctuation stay plain
                }
            }
        }
    }
    if (commentAt != std::string::npos) out.push_back({commentAt, line.size(), pal.comment});
}

} // namespace

namespace {

/// The byte offset of the `n`th character (0-based) of a line; the library counts columns in characters.
size_t byteOfChar(const std::string& s, size_t n) {
    size_t at = 0;
    while (n > 0 && at < s.size()) {
        at++;
        while (at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) at++;
        n--;
    }
    return at;
}

} // namespace

float ScriptGraphEditor::codeX(const std::string& line, int col) const {
    size_t tabs = 0;
    while (tabs < line.size() && line[tabs] == '\t') tabs++;
    const size_t c = static_cast<size_t>(std::clamp(col, 0, static_cast<int>(line.size())));
    const float indent = static_cast<float>(std::min(c, tabs) * kCodeTabColumns) * m_codeCharWidth;
    if (c <= tabs) return indent;
    return static_cast<float>(tabs * kCodeTabColumns) * m_codeCharWidth + ImGui::CalcTextSize(line.c_str() + tabs, line.c_str() + c).x;
}

int ScriptGraphEditor::codeColAt(const std::string& line, float x) const {
    size_t tabs = 0;
    while (tabs < line.size() && line[tabs] == '\t') tabs++;
    const float tabWidth = static_cast<float>(kCodeTabColumns) * m_codeCharWidth;
    const float left = static_cast<float>(tabs) * tabWidth;
    if (x < left) return std::clamp(static_cast<int>(std::lround(x / tabWidth)), 0, static_cast<int>(tabs));
    int col = static_cast<int>(tabs);
    float before = left;
    while (col < static_cast<int>(line.size())) {
        int next = col + 1;
        while (next < static_cast<int>(line.size()) && (static_cast<unsigned char>(line[next]) & 0xC0) == 0x80) next++;
        const float after = left + ImGui::CalcTextSize(line.c_str() + tabs, line.c_str() + next).x;
        if (x < (before + after) * 0.5f) return col;  // nearer to this character's left edge than its right
        col = next;
        before = after;
    }
    return col;
}

ImVec2 ScriptGraphEditor::codeCharPos(int line, int col) const {
    auto row = m_codeRows.find(line);
    if (row == m_codeRows.end() || line < 1 || line > m_code.lineCount()) return ImVec2(0, 0);
    return ImVec2(row->second.x + m_codeGutterWidth + codeX(m_code.lines()[line - 1], col), row->second.y);
}

bool ScriptGraphEditor::codeKeys() {
    ImGuiIO& io = ImGui::GetIO();
    // AltGr arrives as Ctrl+Alt and types characters, so Ctrl only counts on its own.
    const bool ctrl = io.KeyCtrl && !io.KeyAlt;
    const bool shift = io.KeyShift;
    const unsigned revision = m_code.revision();
    const CodeBuffer::Pos cursor = m_code.cursor(), anchor = m_code.anchor();
    auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };

    // Text typed this frame.
    std::string typed;
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        const unsigned int c = io.InputQueueCharacters[i];
        if (c < 32 || c == 127 || ctrl) continue;
        char utf8[5] = {0, 0, 0, 0, 0};
        ImTextCharToUtf8(utf8, c);
        typed += utf8;
    }
    io.InputQueueCharacters.resize(0);
    if (!typed.empty()) m_code.insert(typed);

    if (pressed(ImGuiKey_LeftArrow)) m_code.moveLeft(shift, ctrl);
    if (pressed(ImGuiKey_RightArrow)) m_code.moveRight(shift, ctrl);
    if (pressed(ImGuiKey_UpArrow)) m_code.moveUp(shift);
    if (pressed(ImGuiKey_DownArrow)) m_code.moveDown(shift);
    if (pressed(ImGuiKey_PageUp)) m_code.moveLines(-m_codePageLines, shift);
    if (pressed(ImGuiKey_PageDown)) m_code.moveLines(m_codePageLines, shift);
    if (pressed(ImGuiKey_Home)) m_code.moveHome(shift, ctrl);
    if (pressed(ImGuiKey_End)) m_code.moveEnd(shift, ctrl);

    if (pressed(ImGuiKey_Backspace)) m_code.backspace(ctrl);
    if (pressed(ImGuiKey_Delete)) m_code.del(ctrl);
    if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) m_code.newline();
    if (!ctrl && pressed(ImGuiKey_Tab)) m_code.indent(shift);

    if (ctrl) {
        if (pressed(ImGuiKey_A)) m_code.selectAll();
        if (pressed(ImGuiKey_C) && m_code.hasSelection()) ImGui::SetClipboardText(m_code.selectedText().c_str());
        if (pressed(ImGuiKey_X) && m_code.hasSelection()) {
            ImGui::SetClipboardText(m_code.selectedText().c_str());
            m_code.deleteSelection();
        }
        if (pressed(ImGuiKey_V)) {
            if (const char* clip = ImGui::GetClipboardText()) m_code.insert(clip);
        }
        if (pressed(ImGuiKey_Z)) {
            if (shift) m_code.redo();
            else m_code.undo();
        }
        if (pressed(ImGuiKey_Y)) m_code.redo();
    }
    if (pressed(ImGuiKey_Escape)) m_codeFocused = false;

    return m_code.revision() != revision || m_code.cursor() != cursor || m_code.anchor() != anchor;
}

void ScriptGraphEditor::drawCodePanel() {
    const float em = ImGui::GetFontSize();
    ImGuiIO& io = ImGui::GetIO();
    m_codeRows.clear();

    panelTitle("CODE");
    const float copyWidth = ImGui::CalcTextSize("Copy").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - copyWidth);
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText((m_code.hasSelection() ? m_code.selectedText() : m_code.text()).c_str());
    if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip(m_code.hasSelection() ? "Copy the selected text." : "Copy the script's text.");
    ImGui::Separator();

    if (!m_codeFileOk) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f), "The graph can't show this file right now, so its lines aren't linked to nodes. Fix it here and the graph comes back.");
        ImGui::PopTextWrapPos();
    }

    // Under the text: what is wrong with it, and where the cursor is.
    std::string status;
    ImVec4 statusColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    if (m_codeHasError && (m_codeDirty || !m_codeFileOk)) {
        status = std::string(m_codeDirty ? "Not saved yet. " : "") + "Line " + std::to_string(m_codeError.line) + ": " + m_codeError.message;
        statusColor = ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
    } else if (m_codeDirty) {
        status = "Saving...";
    }
    const float statusWidth = ImGui::GetContentRegionAvail().x;
    float statusHeight = ImGui::GetTextLineHeightWithSpacing();
    if (!status.empty()) statusHeight += ImGui::CalcTextSize(status.c_str(), nullptr, false, statusWidth).y + ImGui::GetStyle().ItemSpacing.y;

    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
    bg = ImVec4(bg.x * 0.75f, bg.y * 0.75f, bg.z * 0.75f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::BeginChild("##CodeScroll", ImVec2(0, -statusHeight), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleColor();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    const float charWidth = ImGui::CalcTextSize("M").x;
    const float rowHeight = std::floor(ImGui::GetTextLineHeight() * 1.3f);
    m_codeCharWidth = charWidth;
    m_codePageLines = (std::max)(1, static_cast<int>(ImGui::GetWindowHeight() / rowHeight) - 1);

    // Typing goes here while the pane has the focus (and the window does).
    if (m_codeFocused && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) m_codeFocused = false;
    if (m_codeFocused) {
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        if (codeKeys()) {
            m_codeFollowCursor = true;
            m_codeActiveAt = ImGui::GetTime();
        }
    }
    if (m_code.revision() != m_codeSeenRevision) codeChanged();
    indexCode();

    const std::vector<std::string>& lines = m_code.lines();
    const int count = static_cast<int>(lines.size());
    int digits = 3;
    for (int n = count; n >= 1000; n /= 10) digits++;
    const float markerWidth = em * 1.0f;
    const float gutterWidth = markerWidth + charWidth * static_cast<float>(digits) + em * 0.8f;
    m_codeGutterWidth = gutterWidth;
    const std::string& longest = lines[m_codeLongest < lines.size() ? m_codeLongest : 0];
    const float textWidth = codeX(longest, static_cast<int>(longest.size()));
    const float rowWidth = (std::max)(ImGui::GetContentRegionAvail().x, gutterWidth + textWidth + em * 3.0f);

    // What is marked: the selected node's lines (while the pane's text is the file's, so lines belong to nodes),
    // and the lines the checker has something to say about.
    const bool linked = codeLinked();
    int selFirst = 0, selLast = 0;
    if (linked) selFirst = codeLineOf(m_selectedKey);
    if (selFirst > 0 && selFirst <= count) {
        // Its sentence, and what is written under it (the lines indented deeper, with the blank lines between).
        auto indentOf = [](const std::string& l) {
            size_t t = 0;
            while (t < l.size() && l[t] == '\t') t++;
            return t;
        };
        const size_t base = indentOf(lines[selFirst - 1]);
        selLast = selFirst;
        for (int l = selFirst + 1; l <= count; l++) {
            const std::string& text = lines[l - 1];
            if (text.empty()) continue;
            if (indentOf(text) <= base) break;
            selLast = l;
        }
    } else {
        selFirst = 0;
    }
    std::map<int, const Problem*> problemAt;
    if (linked) {
        for (const Problem& p : m_liveProblems) {
            auto it = problemAt.find(p.line);
            if (it == problemAt.end() || (p.error && !it->second->error)) problemAt[p.line] = &p;
        }
    }

    const float startY = ImGui::GetCursorPosY();
    // A node picked in the graph: bring its line into view (once, so the pane can be scrolled away after).
    if (m_codeShownKey != m_selectedKey) {
        m_codeShownKey = m_selectedKey;
        if (selFirst > 0) {
            const float rowTop = startY + static_cast<float>(selFirst - 1) * rowHeight;
            const float view = ImGui::GetWindowHeight();
            if (rowTop < ImGui::GetScrollY() + rowHeight) ImGui::SetScrollY((std::max)(0.0f, rowTop - rowHeight * 2.0f));
            else if (rowTop + rowHeight > ImGui::GetScrollY() + view - rowHeight * 2.0f) ImGui::SetScrollY(rowTop + rowHeight * 3.0f - view);
        }
    }
    // The cursor moved (typed, or an arrow key): keep it in view.
    if (m_codeFollowCursor) {
        m_codeFollowCursor = false;
        const CodeBuffer::Pos c = m_code.cursor();
        const float rowTop = startY + static_cast<float>(c.line) * rowHeight;
        const float viewH = ImGui::GetWindowHeight() - ImGui::GetStyle().ScrollbarSize;
        if (rowTop < ImGui::GetScrollY()) ImGui::SetScrollY(rowTop);
        else if (rowTop + rowHeight > ImGui::GetScrollY() + viewH) ImGui::SetScrollY(rowTop + rowHeight - viewH);
        const float cx = gutterWidth + codeX(lines[c.line], c.col);
        const float viewW = ImGui::GetWindowWidth() - ImGui::GetStyle().ScrollbarSize;
        if (cx - ImGui::GetScrollX() < em * 2.0f) ImGui::SetScrollX((std::max)(0.0f, cx - em * 4.0f));
        else if (cx - ImGui::GetScrollX() > viewW - em * 3.0f) ImGui::SetScrollX(cx - viewW + em * 6.0f);
    }

    // One item covers all the text: clicks and drags are turned into positions in it.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##text", ImVec2(rowWidth, rowHeight * static_cast<float>(count)));
    const bool textHovered = ImGui::IsItemHovered();
    auto posAtMouse = [&]() {
        const int line = std::clamp(static_cast<int>(std::floor((io.MousePos.y - origin.y) / rowHeight)), 0, count - 1);
        return CodeBuffer::Pos{line, codeColAt(lines[line], io.MousePos.x - origin.x - gutterWidth)};
    };
    if (ImGui::IsItemActivated()) {
        m_codeFocused = true;
        m_codeActiveAt = ImGui::GetTime();
        m_codeDragging = true;
        const CodeBuffer::Pos p = posAtMouse();
        if (io.MouseClickedCount[0] >= 2) {
            m_code.selectWord(p);
        } else {
            m_code.setCursor(p, io.KeyShift);
            // The node written on that line is the graph's selection too (and comes into view if it is off screen).
            auto owner = m_lineNode.find(p.line + 1);
            if (linked && owner != m_lineNode.end()) selectNodeByKey(owner->second, View::CenterIfHidden);
        }
    } else if (ImGui::IsItemActive() && m_codeDragging && io.MouseClickedCount[0] < 2 && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        m_code.setCursor(posAtMouse(), true);
        m_codeFollowCursor = true;
    }
    if (!io.MouseDown[0]) m_codeDragging = false;
    // A click anywhere else in the window takes the typing back.
    if (m_codeFocused && ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) m_codeFocused = false;
    if (textHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    if (m_codeFocused) {
        // Tell the platform text is being typed (an on-screen keyboard, the IME's place), and the engine's own shortcuts to stand back.
        ImGuiPlatformImeData& ime = ImGui::GetCurrentContext()->PlatformImeData;
        const CodeBuffer::Pos c = m_code.cursor();
        ime.WantVisible = ime.WantTextInput = true;
        ime.InputPos = ImVec2(origin.x + gutterWidth + codeX(lines[c.line], c.col), origin.y + static_cast<float>(c.line) * rowHeight);
        ime.InputLineHeight = rowHeight;
        ime.ViewportId = ImGui::GetWindowViewport()->ID;
    }

    // The rows that are in view.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const CodePalette pal = codePalette();
    const ImU32 numberColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    // What each declared name is colored as: the Variables panel's type colors, softened to sit in text.
    std::map<std::string, ImU32> names;
    for (const VariableInfo& v : m_vars) names[v.name] = mixColor(typeColor(v.type), pal.text, 0.40f);
    for (const ReferenceInfo& r : m_refs) names[r.name] = mixColor(referenceColor(r.kind), pal.text, 0.40f);
    for (const std::string& c : m_needs) names[c] = mixColor(componentColor(c), pal.text, 0.30f);

    const float scrollY = ImGui::GetScrollY();
    const int first = std::clamp(static_cast<int>(std::floor((scrollY - startY) / rowHeight)), 0, count - 1);
    const int last = std::clamp(static_cast<int>(std::ceil((scrollY + ImGui::GetWindowHeight() - startY) / rowHeight)), 0, count - 1);
    const CodeBuffer::Pos selA = m_code.selectionStart(), selB = m_code.selectionEnd();
    const bool blinkOn = std::fmod(ImGui::GetTime() - m_codeActiveAt, 1.0) < 0.6;
    const ImU32 selectionColor = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
    std::vector<CodeSpan> spans;
    for (int i = first; i <= last; i++) {
        const int line = i + 1;
        const std::string& text = lines[i];
        const ImVec2 lo(origin.x, origin.y + static_cast<float>(i) * rowHeight);
        const ImVec2 hi(lo.x + rowWidth, lo.y + rowHeight);
        const float midY = lo.y + rowHeight * 0.5f;
        const float textX = lo.x + gutterWidth;
        m_codeRows[line] = ImVec2(lo.x, midY);

        auto problem = problemAt.find(line);
        const bool typedError = m_codeHasError && m_codeError.line == line;

        // Behind the text: the checker's mark, the node's selection, the line the cursor is on.
        if (problem != problemAt.end()) dl->AddRectFilled(lo, hi, problem->second->error ? IM_COL32(235, 75, 75, 30) : IM_COL32(235, 190, 70, 22));
        if (typedError) dl->AddRectFilled(lo, hi, IM_COL32(235, 75, 75, 36));
        if (selFirst > 0 && line >= selFirst && line <= selLast) {
            dl->AddRectFilled(lo, hi, line == selFirst ? IM_COL32(255, 200, 100, 70) : IM_COL32(255, 200, 100, 24));
            if (line == selFirst) dl->AddRectFilled(lo, ImVec2(lo.x + (std::max)(2.0f, em * 0.15f), hi.y), IM_COL32(255, 200, 100, 255));
        }
        if (m_codeFocused && !m_code.hasSelection() && m_code.cursor().line == i) dl->AddRectFilled(lo, hi, IM_COL32(255, 255, 255, 12));
        if (m_code.hasSelection() && i >= selA.line && i <= selB.line) {
            const float x0 = i == selA.line ? codeX(text, selA.col) : 0.0f;
            const float x1 = i == selB.line ? codeX(text, selB.col) : codeX(text, static_cast<int>(text.size())) + charWidth * 0.6f;
            dl->AddRectFilled(ImVec2(textX + x0, lo.y), ImVec2(textX + x1, hi.y), selectionColor);
        }

        // The gutter: the mark's dot, then the line number.
        const bool bad = problem != problemAt.end() ? problem->second->error : typedError;
        if (problem != problemAt.end() || typedError) {
            dl->AddCircleFilled(ImVec2(lo.x + markerWidth * 0.5f + em * 0.1f, midY), em * 0.22f, bad ? IM_COL32(235, 75, 75, 255) : IM_COL32(235, 190, 70, 255));
        }
        char number[16];
        std::snprintf(number, sizeof(number), "%d", line);
        const float numberWidth = ImGui::CalcTextSize(number).x;
        dl->AddText(ImVec2(lo.x + markerWidth + charWidth * static_cast<float>(digits) - numberWidth, midY - ImGui::GetTextLineHeight() * 0.5f), numberColor, number);

        // The text, one run per color.
        const int section = i < static_cast<int>(m_codeSection.size()) ? m_codeSection[i] : 0;
        highlightLine(text, section, pal, names, spans);
        size_t tabs = 0;
        while (tabs < text.size() && text[tabs] == '\t') tabs++;
        float x = textX + static_cast<float>(tabs * kCodeTabColumns) * charWidth;
        const float y = midY - ImGui::GetTextLineHeight() * 0.5f;
        size_t at = tabs;
        auto run = [&](size_t from, size_t to, ImU32 color) {
            if (to <= from) return;
            const char* a = text.c_str() + from;
            const char* b = text.c_str() + to;
            dl->AddText(ImVec2(x, y), color, a, b);
            x += ImGui::CalcTextSize(a, b).x;
        };
        for (const CodeSpan& s : spans) {
            run(at, s.begin, pal.text);
            run(s.begin, s.end, s.color);
            at = s.end;
        }
        run(at, text.size(), pal.text);

        // Where the text stops being a script: a line under what the library pointed at.
        if (typedError) {
            size_t from = byteOfChar(text, m_codeError.col > 1 ? static_cast<size_t>(m_codeError.col - 1) : tabs);
            from = (std::max)(from, tabs);
            size_t to = from;
            if (m_codeError.col > 1) {
                while (to < text.size() && text[to] != ' ' && text[to] != '\t') to++;
                if (to == from && to < text.size()) to++;
            } else {
                to = text.size();
                while (to > from && (text[to - 1] == ' ' || text[to - 1] == '\t')) to--;
            }
            if (to <= from) to = (std::min)(text.size(), from + 1);
            const float ux0 = textX + codeX(text, static_cast<int>(from)), ux1 = textX + (std::max)(codeX(text, static_cast<int>(to)), codeX(text, static_cast<int>(from)) + charWidth);
            dl->AddLine(ImVec2(ux0, hi.y - 2.0f), ImVec2(ux1, hi.y - 2.0f), IM_COL32(235, 75, 75, 255), (std::max)(1.5f, em * 0.1f));
        }

        // The cursor.
        if (m_codeFocused && blinkOn && m_code.cursor().line == i) {
            const float cx = textX + codeX(text, m_code.cursor().col);
            dl->AddLine(ImVec2(cx, lo.y + 1.0f), ImVec2(cx, hi.y - 1.0f), pal.text, (std::max)(1.5f, em * 0.12f));
        }
    }

    // What a mark says, under the pointer.
    if (textHovered && !m_codeDragging) {
        const int line = std::clamp(static_cast<int>(std::floor((io.MousePos.y - origin.y) / rowHeight)), 0, count - 1) + 1;
        auto problem = problemAt.find(line);
        if (m_codeHasError && m_codeError.line == line) PropulsionMKI::UI::tooltip("%s", m_codeError.message.c_str());
        else if (problem != problemAt.end()) PropulsionMKI::UI::tooltip("%s", problem->second->message.c_str());
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    if (!status.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(status.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    const CodeBuffer::Pos c = m_code.cursor();
    ImGui::TextDisabled("Ln %d, Col %d", c.line + 1, m_code.visualColumn(c) + 1);

    // Typed text that parses is saved once typing pauses, or at once when the pane is left. It goes in after
    // this frame is drawn, like every other edit.
    if (m_codeDirty && !m_codeHasError && !m_applyQueued && m_code.revision() != m_codeFailedRevision) {
        const bool paused = ImGui::GetTime() - m_codeEditedAt >= kCodeApplyDelay;
        if (paused || !m_codeFocused) {
            m_applyQueued = true;
            m_pending.push_back([this]() {
                m_applyQueued = false;
                applyCode();
            });
        }
    }
}

} // namespace Propulsion
