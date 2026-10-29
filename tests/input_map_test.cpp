// Tests for the engine's input as data (include/input/*.h, include/project/InputFiles.h): the key table, Input Actions and
// Input Mappings turning key states into action values, the default input, and the file formats.
//
//   cmake --build . --target InputMapTest --config Debug
//   build/tests/InputMapTest.exe

#include "input/DefaultInput.h"
#include "input/InputMap.h"
#include "project/InputFiles.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

using namespace Propulsion;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

static InputState keys(std::initializer_list<const char*> held, float mouseX = 0.0f, float mouseY = 0.0f) {
    InputState s;
    for (const char* k : held) s.down.insert(k);
    s.mouseX = mouseX;
    s.mouseY = mouseY;
    return s;
}

static int counter = 0;
static std::string nextId() { return "id" + std::to_string(++counter); }

// A small project: one button, one axis, and a table.
struct Fixture {
    std::vector<InputActionDef> actions;
    InputMappingDef mapping;
    InputMap map;

    Fixture() {
        actions = {{"A-jump", "jump", InputValueType::Button, ""}, {"A-walk", "walk", InputValueType::Axis, ""}, {"A-look", "look", InputValueType::Axis, ""}};
        mapping.id = "M";
        mapping.name = "Test";
    }
    Fixture& row(const char* actionId, const char* key, bool negate = false, float scale = 1.0f) {
        mapping.rows.push_back({actionId, key, negate, scale});
        return *this;
    }
    InputMap& build() {
        map.setup(actions, mapping);
        return map;
    }
};

static void testKeyTable() {
    std::set<std::string> names;
    std::set<int> keyCodes, buttonCodes;
    for (const InputKeys::KeyInfo& k : InputKeys::all()) {
        CHECK(names.insert(k.name).second);  // no name twice
        CHECK(!k.name.empty() && k.name.find(' ') == std::string::npos);  // names are saved in files: no spaces
        if (k.kind == InputKeys::Kind::Key) CHECK(keyCodes.insert(k.code).second && k.code > 0);
        if (k.kind == InputKeys::Kind::MouseButton) CHECK(buttonCodes.insert(k.code).second && k.code >= 0);
    }
    CHECK(InputKeys::find("W") && InputKeys::find("W")->code == GLFW_KEY_W);
    CHECK(InputKeys::find("Space") && InputKeys::find("Space")->code == GLFW_KEY_SPACE);
    CHECK(InputKeys::find("F5") && InputKeys::find("F5")->code == GLFW_KEY_F5);
    CHECK(InputKeys::find("7") && InputKeys::find("7")->code == GLFW_KEY_7);
    CHECK(InputKeys::find("MouseLeft") && InputKeys::find("MouseLeft")->kind == InputKeys::Kind::MouseButton);
    CHECK(InputKeys::isAnalog("MouseX") && InputKeys::isAnalog("MouseY") && !InputKeys::isAnalog("W") && !InputKeys::isAnalog("Nope"));
    CHECK(!InputKeys::find("Escape"));  // always stops Play
    CHECK(!InputKeys::find("w") && !InputKeys::find(""));
}

static void testActionNames() {
    CHECK(isValidActionName("jump") && isValidActionName("moveForward") && isValidActionName("Dash2") && isValidActionName("fire_primary"));
    CHECK(!isValidActionName("") && !isValidActionName("2fast") && !isValidActionName("two words") && !isValidActionName("a-b"));
    CHECK(!isValidActionName("Self") && !isValidActionName("is") && !isValidActionName("Input"));
    CHECK(!isValidActionName(std::string(65, 'a')));
    CHECK(actionNameFrom("Move Forward") == "Move_Forward" && isValidActionName(actionNameFrom("Move Forward")));
    CHECK(actionNameFrom("2fast") == "Action2fast");
    CHECK(isValidActionName(actionNameFrom("")) && isValidActionName(actionNameFrom("Self")) && isValidActionName(actionNameFrom("!!!")));
}

static void testAButtonIsDownWhileAnyOfItsKeysIs() {
    Fixture f;
    f.row("A-jump", "Space").row("A-jump", "MouseRight");
    InputMap& m = f.build();
    m.update(keys({}));
    CHECK(!m.pressing("jump") && !m.started("jump") && !m.stopped("jump") && m.axis("jump") == 0.0f);
    m.update(keys({"Space"}));
    CHECK(m.pressing("jump") && m.started("jump") && !m.stopped("jump") && m.axis("jump") == 1.0f);
    m.update(keys({"Space"}));
    CHECK(m.pressing("jump") && !m.started("jump"));  // held: started was one update only
    m.update(keys({"Space", "MouseRight"}));
    CHECK(m.pressing("jump") && !m.started("jump"));  // a second key doesn't start it again
    m.update(keys({"MouseRight"}));
    CHECK(m.pressing("jump") && !m.stopped("jump"));  // still down through the other key
    m.update(keys({}));
    CHECK(!m.pressing("jump") && m.stopped("jump") && !m.started("jump"));
    m.update(keys({}));
    CHECK(!m.stopped("jump"));  // stopped was one update only
    m.update(keys({"F"}));
    CHECK(!m.pressing("jump"));  // a key that isn't bound to it
}

static void testAnAxisAddsUpItsRows() {
    Fixture f;
    f.row("A-walk", "W").row("A-walk", "Up").row("A-walk", "S", true).row("A-walk", "Down", true);
    InputMap& m = f.build();
    m.update(keys({"W"}));
    CHECK(near(m.axis("walk"), 1.0f) && m.pressing("walk"));
    m.update(keys({"S"}));
    CHECK(near(m.axis("walk"), -1.0f));
    m.update(keys({"W", "S"}));
    CHECK(near(m.axis("walk"), 0.0f) && !m.pressing("walk"));  // they cancel
    m.update(keys({"W", "Up"}));
    CHECK(near(m.axis("walk"), 1.0f));  // two keys for the same direction are still 1, not 2
    m.update(keys({"Down", "S"}));
    CHECK(near(m.axis("walk"), -1.0f));
    m.update(keys({}));
    CHECK(near(m.axis("walk"), 0.0f));
}

static void testScale() {
    Fixture f;
    f.row("A-walk", "W", false, 2.5f).row("A-walk", "S", true, 2.5f);
    InputMap& m = f.build();
    m.update(keys({"W"}));
    CHECK(near(m.axis("walk"), 2.5f));
    m.update(keys({"S"}));
    CHECK(near(m.axis("walk"), -2.5f));
    m.update(keys({"W", "S"}));
    CHECK(near(m.axis("walk"), 0.0f));
}

static void testMouseMovement() {
    Fixture f;
    f.row("A-look", "MouseX", false, 2.0f).row("A-look", "Q", true);
    InputMap& m = f.build();
    m.update(keys({}, 0.5f, 3.0f));
    CHECK(near(m.axis("look"), 1.0f));  // the rate times the scale; MouseY isn't bound to it
    m.update(keys({}, -0.25f));
    CHECK(near(m.axis("look"), -0.5f));
    m.update(keys({}, 8.0f));
    CHECK(near(m.axis("look"), 16.0f));  // a mouse has no stop: it isn't held to 1
    m.update(keys({"Q"}, 0.5f));
    CHECK(near(m.axis("look"), 0.0f));  // 1 from the mouse, -1 from the key
    m.update(keys({"Q"}));
    CHECK(near(m.axis("look"), -1.0f));

    Fixture g;  // Y, and Negate on the mouse
    g.row("A-look", "MouseY", true);
    InputMap& n = g.build();
    n.update(keys({}, 5.0f, 0.5f));
    CHECK(near(n.axis("look"), -0.5f));
}

static void testActionsStayApart() {
    Fixture f;
    f.row("A-jump", "Space").row("A-walk", "W").row("Gone", "A").row("A-walk", "NotAKey");
    InputMap& m = f.build();
    m.update(keys({"Space", "A"}));
    CHECK(m.pressing("jump") && !m.pressing("walk") && m.axis("walk") == 0.0f);  // jump's key doesn't walk
    CHECK(!m.pressing("nothing") && m.axis("nothing") == 0.0f && !m.started("nothing") && !m.stopped("nothing"));  // not an action
    CHECK(m.axis("look") == 0.0f);  // an action with no rows
    m.update(keys({"A", "NotAKey"}));
    CHECK(!m.pressing("walk"));  // a row for an action that was deleted, and one with a key that doesn't exist, do nothing

    // A button ignores mouse movement; an axis ignores nothing it was given.
    Fixture g;
    g.row("A-jump", "MouseX");
    InputMap& n = g.build();
    n.update(keys({}, 5.0f));
    CHECK(!n.pressing("jump"));
}

static void testRebinding() {
    Fixture f;
    f.row("A-jump", "Space");
    f.build().update(keys({"Space"}));
    CHECK(f.map.pressing("jump"));
    f.mapping.rows[0].key = "F";  // the player rebinds jump
    InputMap& m = f.build();
    m.update(keys({"Space"}));
    CHECK(!m.pressing("jump"));
    m.update(keys({"F"}));
    CHECK(m.pressing("jump") && m.started("jump"));

    // Setting up again forgets what was down: no "stopped" for something that was held under the old table.
    f.mapping.rows.push_back({"A-jump", "G", false, 1.0f});
    InputMap& again = f.build();
    again.update(keys({}));
    CHECK(!again.stopped("jump"));
}

static void testTheDefaultsAreTheOldKeyTable() {
    counter = 0;
    const InputSetup setup = makeDefaultInput(nextId);
    CHECK(setup.actions.size() == 7 && setup.mappings.size() == 1);
    std::set<std::string> ids;
    for (const InputActionDef& a : setup.actions) {
        CHECK(isValidActionName(a.name) && ids.insert(a.id).second);
    }
    CHECK(ids.insert(setup.mappings[0].id).second);  // every id is different
    CHECK(setup.mappings[0].name == kDefaultMappingName);
    for (const InputRow& r : setup.mappings[0].rows) {
        CHECK(setup.actionById(r.actionId) != nullptr && InputKeys::find(r.key) != nullptr);
    }
    auto typeOf = [&](const char* name) {
        for (const InputActionDef& a : setup.actions) {
            if (a.name == name) return a.type;
        }
        return InputValueType::Button;
    };
    CHECK(typeOf("moveForward") == InputValueType::Axis && typeOf("moveRight") == InputValueType::Axis);
    CHECK(typeOf("lookX") == InputValueType::Axis && typeOf("lookY") == InputValueType::Axis);
    CHECK(typeOf("jump") == InputValueType::Button && typeOf("crouch") == InputValueType::Button && typeOf("interact") == InputValueType::Button);

    InputMap m;
    m.setup(setup.actions, setup.mappings[0]);
    m.update(keys({"W"}));
    CHECK(near(m.axis("moveForward"), 1.0f) && near(m.axis("moveRight"), 0.0f));
    m.update(keys({"Down", "D"}));
    CHECK(near(m.axis("moveForward"), -1.0f) && near(m.axis("moveRight"), 1.0f));
    m.update(keys({"Left", "Up"}));
    CHECK(near(m.axis("moveForward"), 1.0f) && near(m.axis("moveRight"), -1.0f));
    m.update(keys({"A", "S", "Space", "LeftCtrl", "E"}, 0.3f, -0.2f));
    CHECK(near(m.axis("moveRight"), -1.0f) && near(m.axis("moveForward"), -1.0f));
    CHECK(m.pressing("jump") && m.pressing("crouch") && m.pressing("interact"));
    CHECK(near(m.axis("lookX"), 0.3f) && near(m.axis("lookY"), -0.2f));
    m.update(keys({}));
    CHECK(!m.pressing("jump") && m.stopped("jump"));
}

static void testActionFileRoundTrip() {
    InputActionDef in{"guid-1", "fire", InputValueType::Axis, "Pull the trigger"};
    std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
    InputFiles::writeAction(s, in);
    InputActionDef out;
    CHECK(InputFiles::readAction(s, out));
    CHECK(out.id == "guid-1" && out.type == InputValueType::Axis && out.description == "Pull the trigger");
    CHECK(out.name.empty());  // the name is the file's, and the reader doesn't know it
}

static void testMappingFileRoundTrip() {
    InputMappingDef in;
    in.id = "map-1";
    in.rows = {{"a1", "W", false, 1.0f}, {"a1", "S", true, 2.5f}, {"a2", "MouseX", false, 0.25f}, {"a3", "Space", false, 1.0f}};
    std::stringstream s(std::ios::in | std::ios::out | std::ios::binary);
    InputFiles::writeMapping(s, in);
    InputMappingDef out;
    CHECK(InputFiles::readMapping(s, out));
    CHECK(out.id == "map-1" && out.rows == in.rows);

    InputMappingDef empty;
    empty.id = "e";
    std::stringstream t(std::ios::in | std::ios::out | std::ios::binary);
    InputFiles::writeMapping(t, empty);
    InputMappingDef back;
    CHECK(InputFiles::readMapping(t, back) && back.rows.empty() && back.id == "e");
}

static void testDamagedFiles() {
    InputActionDef action{"g", "x", InputValueType::Button, "note"};
    std::stringstream a(std::ios::in | std::ios::out | std::ios::binary);
    InputFiles::writeAction(a, action);
    const std::string abytes = a.str();
    for (size_t cut : {size_t(0), size_t(2), size_t(4), abytes.size() - 1}) {
        std::stringstream s(abytes.substr(0, cut), std::ios::in | std::ios::binary);
        InputActionDef d;
        CHECK(!InputFiles::readAction(s, d));
    }
    std::stringstream notAction("NOPE1234", std::ios::in | std::ios::binary);
    InputActionDef d;
    CHECK(!InputFiles::readAction(notAction, d));

    InputMappingDef mapping;
    mapping.id = "m";
    mapping.rows = {{"a", "W", false, 1.0f}, {"b", "S", true, 1.0f}};
    std::stringstream b(std::ios::in | std::ios::out | std::ios::binary);
    InputFiles::writeMapping(b, mapping);
    const std::string bbytes = b.str();
    for (size_t cut : {size_t(0), size_t(3), size_t(10), bbytes.size() - 3}) {
        std::stringstream s(bbytes.substr(0, cut), std::ios::in | std::ios::binary);
        InputMappingDef m;
        CHECK(!InputFiles::readMapping(s, m));
    }
    // A value type that isn't Button or Axis, and a version that doesn't exist yet, are refused.
    std::stringstream badType(std::ios::in | std::ios::out | std::ios::binary);
    badType.write("IAC", 3);
    ObjectFile::writeValue<uint8_t>(badType, 1);
    ObjectFile::writeString(badType, "g");
    ObjectFile::writeValue<uint8_t>(badType, 7);
    ObjectFile::writeString(badType, "");
    CHECK(!InputFiles::readAction(badType, d));
    std::stringstream future(std::ios::in | std::ios::out | std::ios::binary);
    future.write("IAC", 3);
    ObjectFile::writeValue<uint8_t>(future, 99);
    ObjectFile::writeString(future, "g");
    ObjectFile::writeValue<uint8_t>(future, 0);
    ObjectFile::writeString(future, "");
    CHECK(!InputFiles::readAction(future, d));

    // Files that are each other's kind aren't accepted.
    std::stringstream wrongKind(abytes, std::ios::in | std::ios::binary);
    InputMappingDef m;
    CHECK(!InputFiles::readMapping(wrongKind, m));

    // A huge row count is refused, not allocated; a NaN scale is repaired.
    std::stringstream huge(std::ios::in | std::ios::out | std::ios::binary);
    huge.write("IMP", 3);
    ObjectFile::writeValue<uint8_t>(huge, 1);
    ObjectFile::writeString(huge, "m");
    ObjectFile::writeValue<uint32_t>(huge, 0xFFFFFFFFu);
    CHECK(!InputFiles::readMapping(huge, m));

    InputMappingDef nan;
    nan.id = "n";
    nan.rows = {{"a", "W", false, std::nanf("")}};
    std::stringstream n(std::ios::in | std::ios::out | std::ios::binary);
    InputFiles::writeMapping(n, nan);
    InputMappingDef fixed;
    CHECK(InputFiles::readMapping(n, fixed) && fixed.rows[0].scale == 1.0f);
}

int main() {
    testKeyTable();
    testActionNames();
    testAButtonIsDownWhileAnyOfItsKeysIs();
    testAnAxisAddsUpItsRows();
    testScale();
    testMouseMovement();
    testActionsStayApart();
    testRebinding();
    testTheDefaultsAreTheOldKeyTable();
    testActionFileRoundTrip();
    testMappingFileRoundTrip();
    testDamagedFiles();
    if (failures == 0) std::printf("InputMapTest: all passed\n");
    else std::printf("InputMapTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
