#pragma once

/**
 * DefaultInput.h
 * The Input Actions and Input Mapping a project starts with: what the fixed key table in PlayMode.cpp used to be, as data.
 * A project with no Input Actions gets these (see Engine::ensureDefaultInput), so a player script written before input was
 * data keeps working.
 *
 *   moveForward (axis)  W and Up are +1, S and Down are -1
 *   moveRight (axis)    D and Right are +1, A and Left are -1
 *   lookX, lookY (axis) mouse movement
 *   jump (button)       Space
 *   crouch (button)     Left Ctrl
 *   interact (button)   E
 */

#include "input/InputTypes.h"

#include <functional>
#include <string>

namespace Propulsion {

inline constexpr const char* kDefaultMappingName = "DefaultInput";

/// The default actions and the one mapping that binds them. `newId` makes a fresh GUID each time it is called.
inline InputSetup makeDefaultInput(const std::function<std::string()>& newId) {
    InputSetup setup;
    auto action = [&](const char* name, InputValueType type, const char* description) {
        InputActionDef a;
        a.id = newId();
        a.name = name;
        a.type = type;
        a.description = description;
        setup.actions.push_back(a);
        return a.id;
    };
    const std::string moveForward = action("moveForward", InputValueType::Axis, "Walk forward (+1) or back (-1)");
    const std::string moveRight = action("moveRight", InputValueType::Axis, "Walk right (+1) or left (-1)");
    const std::string lookX = action("lookX", InputValueType::Axis, "Turn: mouse right is positive");
    const std::string lookY = action("lookY", InputValueType::Axis, "Look up and down: mouse up is positive");
    const std::string jump = action("jump", InputValueType::Button, "Jump");
    const std::string crouch = action("crouch", InputValueType::Button, "Crouch");
    const std::string interact = action("interact", InputValueType::Button, "Use or pick up what is in front");

    InputMappingDef mapping;
    mapping.id = newId();
    mapping.name = kDefaultMappingName;
    auto row = [&](const std::string& actionId, const char* key, bool negate = false) {
        InputRow r;
        r.actionId = actionId;
        r.key = key;
        r.negate = negate;
        mapping.rows.push_back(r);
    };
    row(moveForward, "W");
    row(moveForward, "Up");
    row(moveForward, "S", true);
    row(moveForward, "Down", true);
    row(moveRight, "D");
    row(moveRight, "Right");
    row(moveRight, "A", true);
    row(moveRight, "Left", true);
    row(lookX, "MouseX");
    row(lookY, "MouseY");
    row(jump, "Space");
    row(crouch, "LeftCtrl");
    row(interact, "E");
    setup.mappings.push_back(mapping);
    return setup;
}

} // namespace Propulsion
