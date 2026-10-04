/**
 * ScriptNodeRenderer.cpp
 *
 * Draws the pins and the input fields of script nodes (nodes made from one sentence of a ForgeScript
 * file). Layout matches the generic node layout, so hit testing and wires work unchanged:
 *   - one row per pin, inputs on the left and outputs on the right;
 *   - an execution pin is a triangle, a value pin a circle;
 *   - each value input has a text field (or a choice list) next to its label.
 *
 * The widgets only report edits through ScriptGraphHooks; they never change the graph themselves.
 * Hooks may be called while the graph is being drawn, so the host must queue its work.
 */

#include "nodegraph/NodeGraph.h"
#include "ui/ImGuiTheme.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Forge {

namespace {

constexpr float kLabelZone = 106.0f;  // pixels at zoom 1 reserved for an input's label
constexpr float kRightPad = 14.0f;

bool isConnected(const std::vector<NodeConnection>& connections, int nodeId, int pinId) {
    for (const auto& c : connections) {
        if ((c.inputNodeId == nodeId && c.inputPinId == pinId) || (c.outputNodeId == nodeId && c.outputPinId == pinId)) return true;
    }
    return false;
}

ImU32 valuePinColor(const std::string& kind, bool wired) {
    if (kind == "unplugged") return IM_COL32(235, 75, 75, 255);
    if (kind == "condition") return wired ? IM_COL32(240, 130, 130, 255) : IM_COL32(190, 100, 100, 255);
    return wired ? IM_COL32(130, 190, 255, 255) : IM_COL32(150, 165, 190, 255);
}

} // namespace

void NodeGraph::renderScriptNodePins(ImDrawList* drawList, ScriptNode* node, const ImVec2& canvasPos) {
    const float grid = m_gridSize * m_viewZoom;
    const float radius = 6.0f * m_viewZoom;
    const float inset = 0.5f * grid;
    const float pad = 6.0f * m_viewZoom;

    ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
    const float fontSize = font->LegacySize * m_viewZoom;
    const bool showLabels = fontSize > 6.0f;

    ImVec2 nodePos = graphToScreen(ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize), canvasPos);
    ImVec2 nodeSize = ImVec2(node->size.x * grid, node->size.y * grid);
    ImVec2 nodeEnd = ImVec2(nodePos.x + nodeSize.x, nodePos.y + nodeSize.y);
    const float firstRowY = nodePos.y + grid + grid * 0.5f;

    auto glow = [&](const ImVec2& p, bool hovered) {
        if (!hovered) return;
        drawList->AddCircleFilled(p, radius * 2.5f, IM_COL32(255, 180, 100, 80));
        drawList->AddCircleFilled(p, radius * 2.0f, IM_COL32(255, 180, 100, 50));
    };

    auto drawPin = [&](NodePin& pin, const ImVec2& p, const std::string& kind, bool wired) {
        const bool hovered = (m_hoveredPinNodeId == node->id && m_hoveredPinId == pin.id);
        const bool greyed = m_isDraggingPin && !isPinConnectable(node, pin);
        const bool connected = isConnected(m_connections, node->id, pin.id);
        glow(p, hovered && !greyed);
        if (pin.type == PinType::Exec) {
            ImU32 color = greyed ? IM_COL32(80, 80, 80, 255) : IM_COL32(245, 245, 245, 255);
            ImVec2 a(p.x - radius, p.y - radius * 1.1f), b(p.x + radius * 1.2f, p.y), c(p.x - radius, p.y + radius * 1.1f);
            if (connected) {
                drawList->AddTriangleFilled(a, b, c, color);
            } else {
                drawList->AddTriangle(a, b, c, color, 1.8f * m_viewZoom);
            }
        } else {
            ImU32 color = greyed ? IM_COL32(80, 80, 80, 255) : valuePinColor(kind, wired);
            drawList->AddCircleFilled(p, radius * 1.3f, IM_COL32(0, 0, 0, 60));
            drawList->AddCircleFilled(p, radius, color);
            drawList->AddCircle(p, radius, IM_COL32(20, 20, 20, 255), 0, 1.5f * m_viewZoom);
        }
    };

    // Inputs: an execution input first (statements), then one pin per field.
    size_t fieldIndex = 0;
    for (size_t i = 0; i < node->inputs.size(); i++) {
        NodePin& pin = node->inputs[i];
        pin.position = ImVec2(0.5f, 1.5f + static_cast<float>(i));
        ImVec2 p(nodePos.x + inset, firstRowY + static_cast<float>(i) * grid);
        if (pin.type == PinType::Exec) {
            drawPin(pin, p, "", false);
            continue;
        }
        const ScriptPinField* field = fieldIndex < node->fields.size() ? &node->fields[fieldIndex] : nullptr;
        ++fieldIndex;
        const bool wired = field && !field->wiredKey.empty();
        // A choice or a fixed word is a setting, not something a value can be plugged into.
        const bool setting = field && (field->kind == "choice" || field->kind == "flag");
        // `unset` is what a pin with no variable plugged in holds: it is drawn red and empty.
        const bool unplugged = field && !wired && field->text == "unset";
        if (!setting) drawPin(pin, p, unplugged ? std::string("unplugged") : (field ? field->kind : std::string("value")), wired);
        if (showLabels) {
            ImU32 labelColor = wired ? IM_COL32(255, 255, 255, 255) : IM_COL32(175, 175, 180, 255);
            ImVec2 size = ImGui::CalcTextSize(pin.name.c_str());
            float scale = fontSize / font->LegacySize;
            drawList->AddText(font, fontSize, ImVec2(p.x + radius + pad, p.y - size.y * scale * 0.5f), labelColor, pin.name.c_str());
        }
    }

    // Outputs: execution outputs with their labels, or one value output.
    for (size_t i = 0; i < node->outputs.size(); i++) {
        NodePin& pin = node->outputs[i];
        pin.position = ImVec2(node->size.x - 0.5f, 1.5f + static_cast<float>(i));
        ImVec2 p(nodeEnd.x - inset, firstRowY + static_cast<float>(i) * grid);
        drawPin(pin, p, "value", true);
        if (showLabels) {
            ImVec2 size = ImGui::CalcTextSize(pin.name.c_str());
            float scale = fontSize / font->LegacySize;
            drawList->AddText(font, fontSize, ImVec2(p.x - radius - pad - size.x * scale, p.y - size.y * scale * 0.5f), IM_COL32(210, 210, 215, 255), pin.name.c_str());
        }
    }
}

bool NodeGraph::renderSingleScriptNodeWidgets(ScriptNode* node, const ImVec2& canvasPos) {
    bool anyActive = false;
    const float grid = m_gridSize * m_viewZoom;
    const float zoom = m_viewZoom;

    ImVec2 nodePos = graphToScreen(ImVec2(node->position.x * m_gridSize, node->position.y * m_gridSize), canvasPos);
    ImVec2 nodeSize = ImVec2(node->size.x * grid, node->size.y * grid);
    ImVec2 nodeEnd = ImVec2(nodePos.x + nodeSize.x, nodePos.y + nodeSize.y);
    const float firstRowY = nodePos.y + grid + grid * 0.5f;

    // The comment above a sentence, or the problem on its line, shows when the title is hovered.
    {
        ImVec2 hMin = nodePos, hMax = ImVec2(nodeEnd.x, nodePos.y + grid);
        if (ImGui::IsMouseHoveringRect(hMin, hMax) && !ImGui::IsPopupOpen("NodeGraphContextMenu") && !m_isDraggingNode) {
            std::string tip = node->error.empty() ? node->comment : ("Problem: " + node->error);
            if (!tip.empty()) {
                ForgeEngine::UI::tooltip("%s", tip.c_str());
            }
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
    const float textSize = font->LegacySize * zoom;

    // Where each output label sits, per row, so a field stops short of it.
    auto rightReserve = [&](size_t row) -> float {
        if (row < node->outputs.size()) {
            float w = ImGui::CalcTextSize(node->outputs[row].name.c_str()).x * zoom;
            return w + 34.0f * zoom;
        }
        return kRightPad * zoom;
    };

    ImGui::PushClipRect(nodePos, nodeEnd, true);
    ImGui::PushID(node->id);
    ImGui::SetWindowFontScale(zoom);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * zoom, 1.0f * zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * zoom, 2.0f * zoom));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(24, 24, 30, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(36, 36, 46, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(44, 44, 56, 255));

    const size_t rowOffset = node->hasExecIn ? 1 : 0;
    for (size_t i = 0; i < node->fields.size(); i++) {
        ScriptPinField& f = node->fields[i];
        const size_t row = rowOffset + i;
        const float rowY = firstRowY + static_cast<float>(row) * grid;
        const float x0 = nodePos.x + kLabelZone * zoom;
        const float x1 = nodeEnd.x - rightReserve(row);
        const float width = x1 - x0;
        if (width < 20.0f) continue;

        // Zoomed far out, or not editable: just show the text.
        const bool staticText = zoom < 0.55f || !f.editable;
        if (!f.wiredKey.empty() || staticText) {
            // A Get node carries a variable in: the field shows its name. Another node computes it otherwise.
            std::string shown = f.wiredKey.empty() || f.carried ? f.text : std::string(" ");
            ImU32 color = f.wiredKey.empty() ? IM_COL32(210, 210, 215, 255) : IM_COL32(120, 175, 230, 255);
            if (textSize > 6.0f) {
                ImVec2 size = ImGui::CalcTextSize(shown.c_str());
                float scale = textSize / font->LegacySize;
                drawList->PushClipRect(ImVec2(x0, rowY - grid * 0.5f), ImVec2(x1, rowY + grid * 0.5f), true);
                drawList->AddText(font, textSize, ImVec2(x0 + 4.0f * zoom, rowY - size.y * scale * 0.5f), color, shown.c_str());
                drawList->PopClipRect();
            }
            continue;
        }

        const float frameH = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos(ImVec2(x0, rowY - frameH * 0.5f));
        ImGui::SetNextItemWidth(width);
        ImGui::PushID(static_cast<int>(i));

        if (f.picks) {
            // A pin that names a variable: pick it from what the script can use here. (A variable can also be
            // dragged onto it, or wired in from a Get node.)
            const bool unplugged = f.text == "unset";
            if (unplugged) {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(64, 24, 28, 255));
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(215, 150, 150, 255));
            }
            if (beginNodeDropdown("##pick", unplugged ? "pick a variable" : f.text.c_str())) {
                anyActive = true;
                if (f.choices.empty()) ImGui::TextDisabled("Nothing here fits yet. Declare one in Variables.");
                for (const std::string& choice : f.choices) {
                    if (ImGui::Selectable(choice.c_str(), choice == f.text) && choice != f.text && m_scriptHooks.onPinEdited) {
                        m_scriptHooks.onPinEdited(*node, f, choice);
                    }
                }
                ImGui::EndCombo();
            }
            if (unplugged) ImGui::PopStyleColor(2);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FORGE_VARIABLE")) {
                    if (m_scriptHooks.onDropVariableOnPin) m_scriptHooks.onDropVariableOnPin(*node, f, std::string(static_cast<const char*>(payload->Data)));
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) anyActive = true;
        } else if (f.toggle) {
            // A boolean is a checkbox: ticked is true.
            bool on = f.text == "true";
            if (ImGui::Checkbox("##toggle", &on) && m_scriptHooks.onPinEdited) m_scriptHooks.onPinEdited(*node, f, on ? "true" : "false");
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) anyActive = true;
            ImGui::SameLine();
            ImGui::TextUnformatted(f.text.c_str());
        } else if (f.kind == "choice") {
            if (beginNodeDropdown("##choice", f.text.c_str())) {
                anyActive = true;
                for (const std::string& choice : f.choices) {
                    if (ImGui::Selectable(choice.c_str(), choice == f.text) && choice != f.text && m_scriptHooks.onPinEdited) {
                        m_scriptHooks.onPinEdited(*node, f, choice);
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) anyActive = true;
        } else {
            const bool unplugged = f.text == "unset";
            const std::string& shownText = unplugged ? std::string() : f.text;
            if (!f.editing) {
                std::strncpy(f.buffer.data(), shownText.c_str(), f.buffer.size() - 1);
                f.buffer[f.buffer.size() - 1] = '\0';
            }
            if (unplugged) ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(64, 24, 28, 255));
            ImGui::InputTextWithHint("##text", unplugged ? "drop a variable here" : (f.optional ? "(optional)" : ""), f.buffer.data(), f.buffer.size());
            if (unplugged) ImGui::PopStyleColor();
            // A variable dragged from the Variables panel can be dropped on the field.
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FORGE_VARIABLE")) {
                    if (m_scriptHooks.onDropVariableOnPin) m_scriptHooks.onDropVariableOnPin(*node, f, std::string(static_cast<const char*>(payload->Data)));
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemActive()) f.editing = true;
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) anyActive = true;
            if (ImGui::IsItemDeactivated()) {
                f.editing = false;
                if (ImGui::IsItemDeactivatedAfterEdit() && m_scriptHooks.onPinEdited) {
                    std::string typed = f.buffer.data();
                    if (typed != shownText && !(unplugged && typed.empty())) m_scriptHooks.onPinEdited(*node, f, typed);
                }
            }
        }
        ImGui::PopID();
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopID();
    ImGui::PopClipRect();
    return anyActive;
}

} // namespace Forge
