#pragma once

#include "NodeTypes.h"

#include <array>
#include <string>
#include <vector>

namespace Propulsion {

/// One input of a script node: a labelled text field (or a choice list) holding PropulsionScript text.
struct ScriptPinField {
    int pinId = -1;            // the NodePin this field sits beside
    std::string name;
    std::string text;          // the text as it is in the script
    std::string kind;          // value, condition, choice, flag
    bool optional = false;
    bool editable = true;
    bool carried = false;      // a Get node carries a variable into it (shown as the variable's name, wired)
    bool picks = false;        // names a variable: shown as a dropdown of `choices` (what the script can use here)
    bool toggle = false;       // holds true or false: shown as a checkbox (what a Set Variable sets a boolean to)
    std::vector<std::string> choices;
    std::string wiredKey;      // the value node feeding this pin, if its text is a phrase
    std::array<char, 256> buffer{};  // what is being typed while the field is active
    bool editing = false;
};

/// A node made from one sentence of a script (an event, an action, a block, or a computed value).
/// The script text is the source of truth: a ScriptNode is rebuilt from it after every edit.
struct ScriptNode : public Node {
    std::string key;           // the script library's id for the node, like "h0.0.2"
    std::string nodeClass;     // Event, Action, Flow, Data
    std::string category;
    std::string comment;       // the comments written above the sentence
    std::string error;         // the checker's problem on this line, if any
    std::string parentKey;     // for a statement: the node whose body it is in
    int slot = -1;             // ...which of that node's bodies
    int index = -1;            // ...and where in it
    std::string feedsKey;      // for a value node: the statement it feeds, and which pin
    std::string feedsPin;

    /// A "Get <variable>" node dropped from the Variables panel. It is not part of the script: it is a way to
    /// carry a variable to a field, and goes away once it is wired into one.
    bool isGetter = false;

    /// Not connected to the script yet: kept beside it (in the layout file), and not part of its text until it is
    /// wired in (see propulsion_graph_edit's add_floating).
    bool floating = false;

    bool hasExecIn = false;
    std::vector<ScriptPinField> fields;   // one per data input, in pin order (after the exec input)
    std::vector<std::string> execLabels;  // one per exec output, in pin order
    std::vector<std::string> execTargets; // the key each exec output runs next ("" for none)

    ScriptNode(int nodeId, const std::string& title, ImVec2 pos)
        : Node(nodeId, title, "Script", pos, ImVec2(12, 3)) {}

    bool isData() const { return nodeClass == "Data"; }
    bool isEvent() const { return nodeClass == "Event"; }

    /// A Get node, loose or already wired into a pin: its output can be plugged into any value pin, any number
    /// of times (the script only names the variable, so every plug is just that name again).
    bool carriesVariable() const { return isGetter || (isData() && category == "Variables"); }
};

} // namespace Propulsion
