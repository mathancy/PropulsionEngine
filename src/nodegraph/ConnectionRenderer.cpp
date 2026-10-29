/**
 * ConnectionRenderer.cpp
 *
 * Handles rendering of connections between nodes:
 * - Electronic-style connections (horizontal-diagonal-horizontal)
 * - Active connections between pins
 * - Connection being dragged by user
 * - Finding the wire under the mouse (to right-click it and cut it)
 *
 * A wire is a run of straight segments. `electronicPath` / `wirePath` give their corners, and both drawing
 * and clicking use them, so what is highlighted is what a click lands on.
 *
 * Part of the NodeGraph system.
 */

#include "nodegraph/NodeGraph.h"
#include "ui/ImGuiTheme.h"
#include <algorithm>
#include <cmath>

namespace Propulsion {

namespace {

float distanceToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    const float abx = b.x - a.x, aby = b.y - a.y;
    const float lengthSq = abx * abx + aby * aby;
    float t = lengthSq > 0.0f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / lengthSq : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float dx = p.x - (a.x + abx * t), dy = p.y - (a.y + aby * t);
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

// The corners of a wire from an output pin to an input pin: it leaves the output going right and comes into
// the input going right. The pins sit half a grid square inside their nodes, so a wire always has a stub of
// one square from the pin, which is half a square of wire showing outside the node. Three shapes, picked by
// how far apart the pins are:
//   1. 45° diagonal: horizontal -> diagonal -> horizontal, while the two horizontals (sharing whatever is left
//      of dx once the diagonal has taken |dy|) are each at least a stub.
//   2. Otherwise, if the input is two stubs or more to the right of the output: horizontal to the midpoint, a
//      vertical there, horizontal in. Each side still has at least a stub.
//   3. When the input is closer than that (or behind the output): an S. Out a stub, a vertical to the middle
//      height, a horizontal run back, a vertical, in over a stub. At exactly two stubs the run is zero long and
//      this is shape 2, so the wire doesn't jump as a node is dragged across.
static void routeOutputToInput(const ImVec2& out, const ImVec2& in, float stub, std::vector<ImVec2>& points) {
    const float dx = in.x - out.x;
    const float dy = in.y - out.y;
    const float absDy = std::abs(dy);

    // Level with each other: a straight line.
    if (absDy < 2.0f) {
        points = {out, in};
        return;
    }

    // Shape 1. (Half a pixel of slack, so a leftover of exactly two stubs worked out in floats still counts.)
    const float leftover = dx - absDy;
    if (leftover >= 2.0f * stub - 0.5f) {
        const ImVec2 p1(out.x + leftover * 0.5f, out.y);
        const ImVec2 p2(p1.x + absDy, in.y);
        points = {out, p1, p2, in};
        return;
    }

    // Shape 2.
    if (dx >= 2.0f * stub) {
        const float midX = out.x + dx * 0.5f;
        points = {out, ImVec2(midX, out.y), ImVec2(midX, in.y), in};
        return;
    }

    // Shape 3.
    const float midY = (out.y + in.y) * 0.5f;
    points = {out, ImVec2(out.x + stub, out.y), ImVec2(out.x + stub, midY), ImVec2(in.x - stub, midY), ImVec2(in.x - stub, in.y), in};
}

void NodeGraph::electronicPath(const ImVec2& startPos, const ImVec2& endPos, bool startIsOutput, std::vector<ImVec2>& points) const {
    // Always routed output -> input; a wire dragged from an input is the same wire, walked the other way.
    const float stub = m_gridSize * m_viewZoom;  // the pin's inset (half a square) plus half a square outside the node
    if (startIsOutput) {
        routeOutputToInput(startPos, endPos, stub, points);
    } else {
        routeOutputToInput(endPos, startPos, stub, points);
        std::reverse(points.begin(), points.end());
    }
}

// Draw Electronic Nodes style connection: horizontal -> diagonal -> horizontal
void NodeGraph::drawElectronicConnection(ImDrawList* drawList, const ImVec2& startPos, const ImVec2& endPos,
                                          ImU32 lineColor, float lineThickness, bool startIsOutput) {
    std::vector<ImVec2> points;
    electronicPath(startPos, endPos, startIsOutput, points);
    for (size_t i = 0; i + 1 < points.size(); i++) drawList->AddLine(points[i], points[i + 1], lineColor, lineThickness);
}

// The wire of a connection from an output pin to an input pin.
void NodeGraph::wirePath(const Node* outNode, const NodePin& outPin, const ImVec2& startPos, const ImVec2& endPos, std::vector<ImVec2>& points) const {
    // An execution wire to a node below and to the left of where it starts (the next
    // statement) runs out, down through the gap above the target, and in from the left.
    const float z = m_viewZoom;
    if (outPin.type == PinType::Exec && dynamic_cast<const ScriptNode*>(outNode) && endPos.x < startPos.x + 44.0f * z &&
        std::abs(endPos.y - startPos.y) > 2.0f) {
        const float gap = endPos.y - 2.0f * m_gridSize * z;  // the gap above the target's title bar
        const float outX = startPos.x + 22.0f * z;
        const float inX = endPos.x - 22.0f * z;
        points = {startPos, ImVec2(outX, startPos.y), ImVec2(outX, gap), ImVec2(inX, gap), ImVec2(inX, endPos.y), endPos};
    } else {
        electronicPath(startPos, endPos, true, points);
    }
}

bool NodeGraph::connectionEnds(const NodeConnection& conn, const ImVec2& canvasPos, const Node*& outNode, const NodePin*& outPin, ImVec2& start, ImVec2& end) {
    Node* from = getNode(conn.outputNodeId);
    Node* to = getNode(conn.inputNodeId);
    if (!from || !to) return false;
    const NodePin* fromPin = nullptr;
    const NodePin* toPin = nullptr;
    for (const auto& pin : from->outputs) {
        if (pin.id == conn.outputPinId) fromPin = &pin;
    }
    for (const auto& pin : to->inputs) {
        if (pin.id == conn.inputPinId) toPin = &pin;
    }
    if (!fromPin || !toPin) return false;
    outNode = from;
    outPin = fromPin;
    start = getPinScreenPosition(from, *fromPin, canvasPos);
    end = getPinScreenPosition(to, *toPin, canvasPos);
    return true;
}

int NodeGraph::hitTestConnection(const ImVec2& mousePos, const ImVec2& canvasPos) {
    const float reach = std::max(6.0f, 4.0f * m_viewZoom);
    int best = -1;
    float bestDistance = reach;
    std::vector<ImVec2> points;
    for (size_t i = 0; i < m_connections.size(); i++) {
        const Node* outNode = nullptr;
        const NodePin* outPin = nullptr;
        ImVec2 start, end;
        if (!connectionEnds(m_connections[i], canvasPos, outNode, outPin, start, end)) continue;
        wirePath(outNode, *outPin, start, end, points);
        for (size_t k = 0; k + 1 < points.size(); k++) {
            const float d = distanceToSegment(mousePos, points[k], points[k + 1]);
            if (d <= bestDistance) {
                bestDistance = d;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

void NodeGraph::renderConnections(ImDrawList* drawList, const ImVec2& canvasPos) {
    // Connection line color: white
    ImU32 lineColor = IM_COL32(255, 255, 255, 220);
    float lineThickness = PropulsionMKI::UI::scaled(3.0f);

    // The wire under the mouse is lit, so it is clear which one a right-click will cut. (Not while something
    // else is going on, or when the mouse is on a node or a pin.)
    int hovered = -1;
    if (ImGui::IsWindowHovered() && !m_isDraggingPin && !m_isDraggingNode && !m_isBoxSelecting && !ImGui::IsPopupOpen("NodeGraphContextMenu") &&
        m_hoveredNodeId == -1 && m_hoveredPinNodeId == -1) {
        hovered = hitTestConnection(ImGui::GetIO().MousePos, canvasPos);
    }

    std::vector<ImVec2> points;
    for (size_t i = 0; i < m_connections.size(); i++) {
        const NodeConnection& conn = m_connections[i];
        const Node* outNode = nullptr;
        const NodePin* outPin = nullptr;
        ImVec2 startPos, endPos;
        if (!connectionEnds(conn, canvasPos, outNode, outPin, startPos, endPos)) continue;

        // Script graphs: execution order is a thick white wire, a computed value a thinner blue one.
        ImU32 wireColor = lineColor;
        float wireThickness = lineThickness;
        if (outPin->type != PinType::Exec && dynamic_cast<const ScriptNode*>(outNode)) {
            wireColor = IM_COL32(110, 185, 255, 230);
            wireThickness = PropulsionMKI::UI::scaled(2.0f);
        }
        if (static_cast<int>(i) == hovered) {
            wireColor = IM_COL32(255, 190, 90, 255);
            wireThickness += PropulsionMKI::UI::scaled(1.5f);
        }

        wirePath(outNode, *outPin, startPos, endPos, points);
        for (size_t k = 0; k + 1 < points.size(); k++) drawList->AddLine(points[k], points[k + 1], wireColor, wireThickness);
    }
}

void NodeGraph::renderDraggingConnection(ImDrawList* drawList, const ImVec2& canvasPos) {
    if (!m_isDraggingPin) return;

    // Get the start position (the pin we're dragging from)
    Node* startNode = getNode(m_draggingPinNodeId);
    if (!startNode) return;

    // Find the pin
    NodePin* startPin = nullptr;
    if (m_draggingPinIsOutput) {
        for (auto& pin : startNode->outputs) {
            if (pin.id == m_draggingPinId) {
                startPin = &pin;
                break;
            }
        }
    } else {
        for (auto& pin : startNode->inputs) {
            if (pin.id == m_draggingPinId) {
                startPin = &pin;
                break;
            }
        }
    }

    if (!startPin) return;

    ImVec2 startPos = getPinScreenPosition(startNode, *startPin, canvasPos);
    ImVec2 endPos = ImGui::GetIO().MousePos;

    // Connection line color: white
    ImU32 lineColor = IM_COL32(255, 255, 255, 200);
    float lineThickness = PropulsionMKI::UI::scaled(3.0f);  // A couple pixels thick

    // Draw Electronic Nodes style connection
    drawElectronicConnection(drawList, startPos, endPos, lineColor, lineThickness, m_draggingPinIsOutput);
}

} // namespace Propulsion
