#pragma once

/**
 * InputMap.h
 * Turns what the player is doing (keys held, the mouse moving) into the values of Input Actions, through one Input
 * Mapping. Header-only and free of any window, so it is tested on its own (tests/input_map_test.cpp).
 *
 *   Button action  down while any of its keys is held. `started` and `stopped` are true for the one update where it
 *                  went down or came up.
 *   Axis action    the sum of its rows: each held key adds its scale (negated by Negate), and mouse movement adds its
 *                  rate times the scale. Keys alone are held to the largest scale (so W and Up together are still 1,
 *                  not 2); the mouse isn't held to anything, since a mouse has no stop.
 */

#include "input/InputKeys.h"
#include "input/InputTypes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Propulsion {

/// What the player is doing at this moment.
struct InputState {
    std::set<std::string> down;  // names of the keys and mouse buttons held
    float mouseX = 0.0f;         // mouse movement as a rate (see scripting/MouseLook.h): right is positive
    float mouseY = 0.0f;         // up is positive
};

class InputMap {
public:
    /// The actions, and the one mapping whose rows say how to make them happen. Forgets any earlier state.
    void setup(const std::vector<InputActionDef>& actions, const InputMappingDef& mapping) {
        m_actions.clear();
        for (const InputActionDef& a : actions) {
            Action& slot = m_actions[a.name];
            slot.type = a.type;
            for (const InputRow& row : mapping.rows) {
                if (row.actionId == a.id && InputKeys::find(row.key)) slot.rows.push_back(row);
            }
        }
    }

    /// Read the state once per frame; `started` and `stopped` are about the change since the previous update.
    void update(const InputState& state) {
        for (auto& [name, a] : m_actions) {
            const bool wasDown = a.down;
            float digital = 0.0f, analog = 0.0f, limit = 0.0f;
            bool any = false;
            for (const InputRow& row : a.rows) {
                const InputKeys::KeyInfo* key = InputKeys::find(row.key);
                const float sign = row.negate ? -1.0f : 1.0f;
                if (key->kind == InputKeys::Kind::MouseAxis) {
                    if (a.type == InputValueType::Axis) analog += (key->name == "MouseX" ? state.mouseX : state.mouseY) * row.scale * sign;
                } else if (state.down.count(row.key)) {
                    any = true;
                    digital += row.scale * sign;
                }
                if (key->kind != InputKeys::Kind::MouseAxis) limit = (std::max)(limit, std::fabs(row.scale));
            }
            if (a.type == InputValueType::Button) {
                a.down = any;
                a.value = any ? 1.0f : 0.0f;
            } else {
                digital = (std::max)(-limit, (std::min)(limit, digital));
                a.value = digital + analog;
                a.down = a.value != 0.0f;
            }
            a.started = a.down && !wasDown;
            a.stopped = !a.down && wasDown;
        }
    }

    // What an action is now. Anything that isn't an action of this mapping reads as nothing.
    bool pressing(const std::string& action) const { const Action* a = find(action); return a && a->down; }
    bool started(const std::string& action) const { const Action* a = find(action); return a && a->started; }
    bool stopped(const std::string& action) const { const Action* a = find(action); return a && a->stopped; }
    float axis(const std::string& action) const { const Action* a = find(action); return a ? a->value : 0.0f; }

private:
    struct Action {
        InputValueType type = InputValueType::Button;
        std::vector<InputRow> rows;
        bool down = false, started = false, stopped = false;
        float value = 0.0f;
    };
    const Action* find(const std::string& name) const {
        auto it = m_actions.find(name);
        return it == m_actions.end() ? nullptr : &it->second;
    }
    std::map<std::string, Action> m_actions;
};

} // namespace Propulsion
