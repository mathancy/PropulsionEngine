#pragma once

/**
 * InputTypes.h
 * The data behind the engine's input, modeled on Unreal's Enhanced Input.
 *
 *   Input Action   a thing the player can do ("jump", "moveForward"), one asset each. Its file name is the name scripts
 *                  use (`Input is pressing jump`, `axis moveForward of Input`). It is a Button (down or not) or an
 *                  Axis (a number, usually between -1 and 1).
 *   Input Mapping  one table saying which keys make which actions happen. Each row is `action <- key`, with
 *                  modifiers: Negate (flip the sign) and Scale (multiply). An Object picks the mapping it is played with.
 *
 * Header-only, so it can be tested without a window (tests/input_map_test.cpp).
 */

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace Propulsion {

enum class InputValueType : uint8_t { Button = 0, Axis = 1 };

struct InputActionDef {
    std::string id;           // GUID, kept in the asset's file
    std::string name;         // the file name: what scripts call it
    InputValueType type = InputValueType::Button;
    std::string description;  // a note for people (shown in the editor)

    bool operator==(const InputActionDef& o) const {
        return id == o.id && name == o.name && type == o.type && description == o.description;
    }
};

/// One row of a mapping table.
struct InputRow {
    std::string actionId;  // the Input Action's GUID (so renaming an action doesn't break a mapping)
    std::string key;       // a key's name (see InputKeys.h): "W", "Space", "MouseLeft", "MouseX"
    bool negate = false;   // flip the sign: S on `moveForward` is the W row with Negate
    float scale = 1.0f;    // multiply the value (a mouse sensitivity, or a faster walk)

    bool operator==(const InputRow& o) const {
        return actionId == o.actionId && key == o.key && negate == o.negate && scale == o.scale;
    }
};

struct InputMappingDef {
    std::string id;    // GUID
    std::string name;  // the file name
    std::vector<InputRow> rows;

    bool operator==(const InputMappingDef& o) const { return id == o.id && name == o.name && rows == o.rows; }
};

/// All the project's input, as Play needs it.
struct InputSetup {
    std::vector<InputActionDef> actions;
    std::vector<InputMappingDef> mappings;

    const InputActionDef* actionById(const std::string& id) const {
        for (const InputActionDef& a : actions) {
            if (a.id == id) return &a;
        }
        return nullptr;
    }
    const InputMappingDef* mappingById(const std::string& id) const {
        for (const InputMappingDef& m : mappings) {
            if (m.id == id) return &m;
        }
        return nullptr;
    }
};

/// Whether `name` can be the name of an Input Action: a name PropulsionScript accepts (letters, digits and underscores,
/// starting with a letter) that isn't one of the words its sentences are made of.
inline bool isValidActionName(const std::string& name) {
    if (name.empty() || name.size() > 64 || !std::isalpha(static_cast<unsigned char>(name[0]))) return false;
    for (char ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') return false;
    }
    static const char* const words[] = {"Self", "unset", "true", "false", "present", "missing", "is", "not", "and", "or",
                                        "then", "if", "otherwise", "of", "to", "from", "by", "in", "as", "per", "second",
                                        "seconds", "pressing", "starts", "stops", "axis", "Input"};
    for (const char* w : words) {
        if (name == w) return false;
    }
    return true;
}

/// A name made from whatever someone typed, that is valid: anything else becomes `_`, and a leading digit gets an
/// `Action` in front. Used when a file is created, so its name works in scripts.
inline std::string actionNameFrom(const std::string& typed) {
    std::string name;
    for (char ch : typed) name += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') ? ch : '_';
    if (name.empty() || !std::isalpha(static_cast<unsigned char>(name[0]))) name = "Action" + name;
    if (!isValidActionName(name)) name += "Action";
    return name;
}

} // namespace Propulsion
