#pragma once

/**
 * InputKeys.h
 * The keys a mapping row can name, and what each one is for the window system (GLFW). Rows store the key's *name*
 * ("W", "Space", "MouseLeft"), so a saved mapping doesn't depend on any library's numbering.
 *
 *   Key          a keyboard key: down or up
 *   MouseButton  a mouse button: down or up
 *   MouseAxis    mouse movement, `MouseX` and `MouseY`: a rate, as described in scripting/MouseLook.h (right and up
 *                are positive). Only Axis actions can use these.
 *
 * Escape isn't here: it always stops Play.
 */

#include <GLFW/glfw3.h>

#include <string>
#include <vector>

namespace Propulsion {
namespace InputKeys {

enum class Kind { Key, MouseButton, MouseAxis };

struct KeyInfo {
    std::string name;
    Kind kind;
    int code;  // GLFW_KEY_* or GLFW_MOUSE_BUTTON_*; unused (-1) for the mouse axes
};

inline const std::vector<KeyInfo>& all() {
    static const std::vector<KeyInfo> keys = [] {
        std::vector<KeyInfo> k;
        for (char c = 'A'; c <= 'Z'; ++c) k.push_back({std::string(1, c), Kind::Key, GLFW_KEY_A + (c - 'A')});
        for (char c = '0'; c <= '9'; ++c) k.push_back({std::string(1, c), Kind::Key, GLFW_KEY_0 + (c - '0')});
        for (int f = 1; f <= 12; ++f) k.push_back({"F" + std::to_string(f), Kind::Key, GLFW_KEY_F1 + (f - 1)});
        const KeyInfo others[] = {
            {"Space", Kind::Key, GLFW_KEY_SPACE},
            {"Enter", Kind::Key, GLFW_KEY_ENTER},
            {"Tab", Kind::Key, GLFW_KEY_TAB},
            {"Backspace", Kind::Key, GLFW_KEY_BACKSPACE},
            {"Up", Kind::Key, GLFW_KEY_UP},
            {"Down", Kind::Key, GLFW_KEY_DOWN},
            {"Left", Kind::Key, GLFW_KEY_LEFT},
            {"Right", Kind::Key, GLFW_KEY_RIGHT},
            {"LeftShift", Kind::Key, GLFW_KEY_LEFT_SHIFT},
            {"RightShift", Kind::Key, GLFW_KEY_RIGHT_SHIFT},
            {"LeftCtrl", Kind::Key, GLFW_KEY_LEFT_CONTROL},
            {"RightCtrl", Kind::Key, GLFW_KEY_RIGHT_CONTROL},
            {"LeftAlt", Kind::Key, GLFW_KEY_LEFT_ALT},
            {"RightAlt", Kind::Key, GLFW_KEY_RIGHT_ALT},
            {"Insert", Kind::Key, GLFW_KEY_INSERT},
            {"Delete", Kind::Key, GLFW_KEY_DELETE},
            {"Home", Kind::Key, GLFW_KEY_HOME},
            {"End", Kind::Key, GLFW_KEY_END},
            {"PageUp", Kind::Key, GLFW_KEY_PAGE_UP},
            {"PageDown", Kind::Key, GLFW_KEY_PAGE_DOWN},
            {"Comma", Kind::Key, GLFW_KEY_COMMA},
            {"Period", Kind::Key, GLFW_KEY_PERIOD},
            {"Slash", Kind::Key, GLFW_KEY_SLASH},
            {"Semicolon", Kind::Key, GLFW_KEY_SEMICOLON},
            {"Apostrophe", Kind::Key, GLFW_KEY_APOSTROPHE},
            {"LeftBracket", Kind::Key, GLFW_KEY_LEFT_BRACKET},
            {"RightBracket", Kind::Key, GLFW_KEY_RIGHT_BRACKET},
            {"Backslash", Kind::Key, GLFW_KEY_BACKSLASH},
            {"Minus", Kind::Key, GLFW_KEY_MINUS},
            {"Equals", Kind::Key, GLFW_KEY_EQUAL},
            {"Tilde", Kind::Key, GLFW_KEY_GRAVE_ACCENT},
            {"MouseLeft", Kind::MouseButton, GLFW_MOUSE_BUTTON_LEFT},
            {"MouseRight", Kind::MouseButton, GLFW_MOUSE_BUTTON_RIGHT},
            {"MouseMiddle", Kind::MouseButton, GLFW_MOUSE_BUTTON_MIDDLE},
            {"Mouse4", Kind::MouseButton, GLFW_MOUSE_BUTTON_4},
            {"Mouse5", Kind::MouseButton, GLFW_MOUSE_BUTTON_5},
            {"MouseX", Kind::MouseAxis, -1},
            {"MouseY", Kind::MouseAxis, -1},
        };
        for (const KeyInfo& info : others) k.push_back(info);
        return k;
    }();
    return keys;
}

/// The key with this name, or null if there isn't one.
inline const KeyInfo* find(const std::string& name) {
    for (const KeyInfo& k : all()) {
        if (k.name == name) return &k;
    }
    return nullptr;
}

/// Whether the key can drive an Axis action only (mouse movement).
inline bool isAnalog(const std::string& name) {
    const KeyInfo* k = find(name);
    return k && k->kind == Kind::MouseAxis;
}

} // namespace InputKeys
} // namespace Propulsion
