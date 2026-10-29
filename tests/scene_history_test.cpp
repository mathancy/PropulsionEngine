// Tests for duplicating a scene object: the copy's name, where it is put, what it keeps, and undo and redo (header only, no editor).
//
//   cmake --build . --target SceneHistoryTest --config Debug
//   build/tests/SceneHistoryTest.exe

#include "core/SceneHistory.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace Propulsion;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static std::string nameFor(const std::string& name, const std::set<std::string>& taken) {
    return copyNameFor(name, [&](const std::string& candidate) { return taken.count(candidate) != 0; });
}

static void testNames() {
    CHECK(nameFor("Cube_01", {"Cube_01"}) == "Cube_02");                       // the next number
    CHECK(nameFor("Cube_01", {"Cube_01", "Cube_02", "Cube_03"}) == "Cube_04");  // skipping what is taken
    CHECK(nameFor("Cube_03", {"Cube_03"}) == "Cube_04");                       // from the original's number, not from 1
    CHECK(nameFor("Cube_09", {"Cube_09"}) == "Cube_10");                       // two digits stay two digits
    CHECK(nameFor("Cube_99", {"Cube_99"}) == "Cube_100");
    CHECK(nameFor("Player", {"Player"}) == "Player_01");                       // no counter yet
    CHECK(nameFor("Player", {"Player", "Player_01"}) == "Player_02");
    CHECK(nameFor("Rock_Big_07", {"Rock_Big_07"}) == "Rock_Big_08");          // only the last part is a counter
    CHECK(nameFor("Wall_2", {"Wall_2"}) == "Wall_03");                         // written the way new objects are named
    CHECK(nameFor("Box2", {"Box2"}) == "Box2_01");                             // digits without an underscore are part of the name
    CHECK(nameFor("_5", {"_5"}) == "_5_01");                                   // nothing before the underscore: not a counter
    CHECK(nameFor("Cube_01", {}) == "Cube_02");                                // free names are used as they are
}

static void testCopy() {
    SceneObject original("Crate_01");
    original.position = glm::vec3(1, 2, 3);
    original.rotation = glm::vec3(10, 20, 30);
    original.quaternion = glm::quat(glm::vec3(0.1f, 0.2f, 0.3f));
    original.scale = glm::vec3(2, 3, 4);
    original.color = glm::vec3(0.1f, 0.5f, 0.9f);
    original.visible = false;
    original.sourceObjectIndex = 7;
    original.sourceObjectGUID = "object-guid";
    original.meshGUID = "mesh-guid";
    original.materialGUID = "material-guid";
    original.overridesMesh = true;
    original.overridesMaterial = true;
    // What only exists while playing.
    original.scriptFile = "Scripts/player.prop";
    original.inputMappingId = "mapping";
    original.collisionEnabled = false;
    original.lookPitch = 33.0f;

    SceneObject copy = copyOfSceneObject(original, "Crate_02");
    CHECK(copy.name == "Crate_02");
    CHECK(copy.position == glm::vec3(1, 2, 3));                                  // exactly where the original is
    CHECK(copy.rotation == original.rotation && copy.quaternion == original.quaternion && copy.scale == original.scale);
    CHECK(copy.color == original.color && copy.visible == original.visible);
    CHECK(copy.sourceObjectIndex == 7 && copy.sourceObjectGUID == "object-guid");  // an instance of the same Object asset
    CHECK(copy.meshGUID == "mesh-guid" && copy.materialGUID == "material-guid");
    CHECK(copy.overridesMesh && copy.overridesMaterial);                          // its own choices come with it
    CHECK(copy.scriptFile.empty() && copy.inputMappingId.empty() && copy.collisionEnabled && copy.lookPitch == 0.0f);

    // The original is untouched.
    CHECK(original.name == "Crate_01" && original.position == glm::vec3(1, 2, 3) && original.scriptFile == "Scripts/player.prop");
}

static std::vector<SceneObject> sceneOf(std::initializer_list<const char*> names) {
    std::vector<SceneObject> scene;
    for (const char* n : names) scene.push_back(SceneObject(n));
    return scene;
}

/// What Engine::duplicateObject does to the scene and the history.
static void duplicate(std::vector<SceneObject>& scene, int& selected, std::vector<UndoState>& undo, std::vector<UndoState>& redo, int index) {
    const SceneObject source = scene[index];
    const std::string name = copyNameFor(source.name, [&](const std::string& c) {
        for (const SceneObject& o : scene) {
            if (o.name == c) return true;
        }
        return false;
    });
    scene.push_back(copyOfSceneObject(source, name));
    recordAddedObject(undo, redo, static_cast<int>(scene.size()) - 1, scene.back(), selected);
    selected = static_cast<int>(scene.size()) - 1;
}

static void testUndoAndRedo() {
    std::vector<SceneObject> scene = sceneOf({"A_01", "B_01"});
    scene[0].position = glm::vec3(1, 2, 3);
    int selected = 0;
    std::vector<UndoState> undo, redo;

    duplicate(scene, selected, undo, redo, 0);
    CHECK(scene.size() == 3 && scene[2].name == "A_02" && selected == 2);
    CHECK(scene[2].position == scene[0].position);  // in the same place
    CHECK(undo.size() == 1 && redo.empty());

    // Undo takes the copy out and selects what was selected before.
    CHECK(undoSceneChange(undo.back(), scene, selected, redo));
    undo.pop_back();
    CHECK(scene.size() == 2 && scene[0].name == "A_01" && scene[1].name == "B_01");
    CHECK(selected == 0);
    CHECK(undo.empty() && redo.size() == 1);

    // Redo puts it back, selected, with the step back on the undo side.
    CHECK(redoSceneChange(redo.back(), scene, selected, undo));
    redo.pop_back();
    CHECK(scene.size() == 3 && scene[2].name == "A_02" && selected == 2);
    CHECK(undo.size() == 1 && redo.empty());

    // The copy as it is when undone comes back as it was: moved copies stay moved.
    scene[2].position = glm::vec3(9, 9, 9);
    CHECK(undoSceneChange(undo.back(), scene, selected, redo));
    undo.pop_back();
    CHECK(redoSceneChange(redo.back(), scene, selected, undo));
    redo.pop_back();
    CHECK(scene[2].position == glm::vec3(9, 9, 9));
}

static void testSeveralInARow() {
    std::vector<SceneObject> scene = sceneOf({"A_01"});
    int selected = 0;
    std::vector<UndoState> undo, redo;
    duplicate(scene, selected, undo, redo, 0);  // A_02, selected
    duplicate(scene, selected, undo, redo, 1);  // A_03, a copy of the copy
    CHECK(scene.size() == 3 && scene[2].name == "A_03" && undo.size() == 2);
    CHECK(undo[1].selectionBefore == 1);

    // Last in, first out.
    CHECK(undoSceneChange(undo.back(), scene, selected, redo));
    undo.pop_back();
    CHECK(scene.size() == 2 && scene[1].name == "A_02" && selected == 1);
    CHECK(undoSceneChange(undo.back(), scene, selected, redo));
    undo.pop_back();
    CHECK(scene.size() == 1 && scene[0].name == "A_01" && selected == 0 && undo.empty() && redo.size() == 2);

    // And back again, in the order they were made.
    CHECK(redoSceneChange(redo.back(), scene, selected, undo));
    redo.pop_back();
    CHECK(redoSceneChange(redo.back(), scene, selected, undo));
    redo.pop_back();
    CHECK(scene.size() == 3 && scene[1].name == "A_02" && scene[2].name == "A_03" && selected == 2);

    // A new action ends the redo history.
    CHECK(undoSceneChange(undo.back(), scene, selected, redo));
    undo.pop_back();
    CHECK(redo.size() == 1);
    duplicate(scene, selected, undo, redo, 0);
    CHECK(redo.empty());
}

static void testNothingSelectedBefore() {
    std::vector<SceneObject> scene = sceneOf({"A_01"});
    int selected = -1;
    std::vector<UndoState> undo, redo;
    duplicate(scene, selected, undo, redo, 0);
    CHECK(undoSceneChange(undo.back(), scene, selected, redo));
    CHECK(selected == -1);  // what was selected before: nothing
}

static void testAStaleStepChangesNothing() {
    std::vector<SceneObject> scene = sceneOf({"A_01"});
    int selected = 0;
    std::vector<UndoState> undo, redo;
    duplicate(scene, selected, undo, redo, 0);

    // The object at that position is a different one (the scene was changed some other way): leave it alone.
    scene[1].name = "Renamed";
    CHECK(!undoSceneChange(undo.back(), scene, selected, redo));
    CHECK(scene.size() == 2 && scene[1].name == "Renamed" && redo.empty());

    // The object is gone altogether.
    scene.pop_back();
    CHECK(!undoSceneChange(undo.back(), scene, selected, redo) && scene.size() == 1);

    // A step that is not an added object is not one.
    UndoState move(0, glm::vec3(0), glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1));
    CHECK(!undoSceneChange(move, scene, selected, redo) && !redoSceneChange(move, scene, selected, undo));
    // Redo with a position past the end of the scene is refused.
    UndoState beyond = undo.back();
    beyond.objectIndex = 5;
    CHECK(!redoSceneChange(beyond, scene, selected, undo));
}

static void testTheHistoryIsLimited() {
    std::vector<SceneObject> scene = sceneOf({"A_01"});
    int selected = 0;
    std::vector<UndoState> undo, redo;
    for (int i = 0; i < 120; i++) duplicate(scene, selected, undo, redo, 0);
    CHECK(undo.size() == 100);
}

// ---- deleting ----

static bool undoStep(std::vector<SceneObject>& scene, int& selected, std::vector<UndoState>& undo, std::vector<UndoState>& redo) {
    if (undo.empty()) return false;
    const UndoState step = undo.back();
    undo.pop_back();
    return undoSceneChange(step, scene, selected, redo);
}

static bool redoStep(std::vector<SceneObject>& scene, int& selected, std::vector<UndoState>& undo, std::vector<UndoState>& redo) {
    if (redo.empty()) return false;
    const UndoState step = redo.back();
    redo.pop_back();
    return redoSceneChange(step, scene, selected, undo);
}

/// What Engine::deleteObject does to the scene and the history.
static void remove(std::vector<SceneObject>& scene, int& selected, std::vector<UndoState>& undo, std::vector<UndoState>& redo, int index) {
    recordRemovedObject(undo, redo, index, scene[index], selected);
    scene.erase(scene.begin() + index);
    selected = -1;
}

static std::string names(const std::vector<SceneObject>& scene) {
    std::string out;
    for (const SceneObject& o : scene) out += o.name + ",";
    return out;
}

static void testDeleteAndUndo() {
    std::vector<SceneObject> scene = sceneOf({"A", "B", "C"});
    scene[1].position = glm::vec3(4, 5, 6);
    scene[1].scale = glm::vec3(2, 2, 2);
    int selected = 1;
    std::vector<UndoState> undo, redo;

    remove(scene, selected, undo, redo, 1);
    CHECK(names(scene) == "A,C," && selected == -1 && undo.size() == 1);

    // Undo brings it back in the same place in the list, as it was, selected.
    CHECK(undoStep(scene, selected, undo, redo));
    CHECK(names(scene) == "A,B,C," && selected == 1);
    CHECK(scene[1].position == glm::vec3(4, 5, 6) && scene[1].scale == glm::vec3(2, 2, 2));
    CHECK(undo.empty() && redo.size() == 1);

    // Redo deletes it again (nothing selected), and undo once more brings it back.
    CHECK(redoStep(scene, selected, undo, redo));
    CHECK(names(scene) == "A,C," && selected == -1 && undo.size() == 1 && redo.empty());
    CHECK(undoStep(scene, selected, undo, redo));
    CHECK(names(scene) == "A,B,C,");

    // The first and the last objects too.
    remove(scene, selected, undo, redo, 0);
    CHECK(names(scene) == "B,C,");
    CHECK(undoStep(scene, selected, undo, redo) && names(scene) == "A,B,C,");
    remove(scene, selected, undo, redo, 2);
    CHECK(names(scene) == "A,B,");
    CHECK(undoStep(scene, selected, undo, redo) && names(scene) == "A,B,C,");
}

static void testEarlierStepsStillFitAfterADeleteIsUndone() {
    // A move of C is recorded (by position: 2), then B is deleted, which makes C the object at position 1. Undoing the delete first
    // puts the scene back as it was, so the move's position 2 is C again when its turn comes.
    std::vector<SceneObject> scene = sceneOf({"A", "B", "C"});
    int selected = 2;
    std::vector<UndoState> undo, redo;
    undo.emplace_back(2, scene[2].position, scene[2].rotation, scene[2].quaternion, scene[2].scale);  // the move, of C
    const std::string moved = scene[undo.back().objectIndex].name;

    remove(scene, selected, undo, redo, 1);
    CHECK(scene[1].name == "C");  // positions have shifted
    CHECK(undoStep(scene, selected, undo, redo));
    CHECK(scene[undo.back().objectIndex].name == moved);  // and shifted back before the move comes round
}

static void testDeletingAfterAnUndoEndsTheRedoHistory() {
    std::vector<SceneObject> scene = sceneOf({"A", "B"});
    int selected = 0;
    std::vector<UndoState> undo, redo;
    duplicate(scene, selected, undo, redo, 0);
    CHECK(undoStep(scene, selected, undo, redo) && redo.size() == 1);
    remove(scene, selected, undo, redo, 1);
    CHECK(redo.empty());
}

static void testStaleRemovals() {
    std::vector<SceneObject> scene = sceneOf({"A", "B"});
    int selected = 0;
    std::vector<UndoState> undo, redo;
    remove(scene, selected, undo, redo, 0);
    // Undo with a position past the end of the list is refused (it would not be where it was).
    undo.back().objectIndex = static_cast<int>(scene.size()) + 1;  // one past the place right after the last one
    CHECK(!undoStep(scene, selected, undo, redo) && names(scene) == "B,");
    // Redo of a deletion of something that is not there any more.
    UndoState step(0, glm::vec3(0), glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1));
    step.kind = UndoState::Kind::Removed;
    step.object = std::make_shared<SceneObject>("Gone");
    std::vector<UndoState> none;
    CHECK(!redoSceneChange(step, scene, selected, none) && names(scene) == "B," && none.empty());
}

static bool edited(std::vector<SceneObject>& scene, int& selected, std::vector<UndoState>& undo, std::vector<UndoState>& redo, int index, const SceneObject& before);

/// The names, with a mark on the hidden ones: what the model test compares.
static std::string describe(const std::vector<SceneObject>& scene) {
    std::string out;
    for (const SceneObject& o : scene) out += o.name + (o.visible ? "," : "~,");
    return out;
}

/// Random adds, deletes, undos and redos against a plain model of the history: after every one the scene must be what the model says.
static void testAgainstAModel() {
    unsigned state = 12345;
    auto next = [&](unsigned n) {
        state = state * 1103515245u + 12345u;
        return (state >> 16) % n;
    };
    for (int round = 0; round < 40; round++) {
        std::vector<SceneObject> scene = sceneOf({"A_01", "B_01", "C_01"});
        int selected = -1;
        std::vector<UndoState> undo, redo;
        std::vector<std::string> states = {describe(scene)};  // the scene after each step still in the history
        size_t at = 0;                                      // which of them the scene is at
        for (int op = 0; op < 60; op++) {
            const unsigned what = next(7);
            if (what <= 1 && !scene.empty() && scene.size() < 12) {
                duplicate(scene, selected, undo, redo, static_cast<int>(next(static_cast<unsigned>(scene.size()))));
                states.resize(at + 1);
                states.push_back(describe(scene));
                at++;
            } else if (what == 2 && !scene.empty()) {
                remove(scene, selected, undo, redo, static_cast<int>(next(static_cast<unsigned>(scene.size()))));
                states.resize(at + 1);
                states.push_back(describe(scene));
                at++;
            } else if (what == 3 && !scene.empty()) {
                // An edit: a rename to a name nothing else has, or hiding or showing.
                const int index = static_cast<int>(next(static_cast<unsigned>(scene.size())));
                const SceneObject was = scene[index];
                if (next(2) == 0) scene[index].name = "N" + std::to_string(round * 1000 + op) + "_01";
                else scene[index].visible = !scene[index].visible;
                CHECK(edited(scene, selected, undo, redo, index, was));
                states.resize(at + 1);
                states.push_back(describe(scene));
                at++;
            } else if (what <= 4) {
                const bool did = undoStep(scene, selected, undo, redo);
                CHECK(did == (at > 0));
                if (did) at--;
            } else if (what >= 5) {
                const bool did = redoStep(scene, selected, undo, redo);
                CHECK(did == (at + 1 < states.size()));
                if (did) at++;
            }
            if (describe(scene) != states[at]) {
                std::printf("model differs in round %d op %d: %s vs %s\n", round, op, describe(scene).c_str(), states[at].c_str());
                CHECK(false);
                return;
            }
            CHECK(selected >= -1 && selected < static_cast<int>(scene.size()));
        }
    }
}

// ---- editing ----

/// What Engine::recordObjectEdit does: `before` is how the object at `index` was, the scene has it as it is now.
static bool edited(std::vector<SceneObject>& scene, int& selected, std::vector<UndoState>& undo, std::vector<UndoState>& redo, int index, const SceneObject& before) {
    return recordObjectEdit(undo, redo, index, before, scene[index], selected);
}

static void testEditsUndoAndRedo() {
    std::vector<SceneObject> scene = sceneOf({"A", "B", "C"});
    int selected = 0;
    std::vector<UndoState> undo, redo;

    // A rename.
    SceneObject before = scene[1];
    scene[1].name = "Bee";
    CHECK(edited(scene, selected, undo, redo, 1, before));
    CHECK(undo.size() == 1 && undo.back().kind == UndoState::Kind::Edited);

    // Visibility, and a mesh and material override.
    before = scene[1];
    scene[1].visible = false;
    CHECK(edited(scene, selected, undo, redo, 1, before));
    before = scene[2];
    scene[2].meshGUID = "mesh-2";
    scene[2].overridesMesh = true;
    scene[2].materialGUID = "mat-2";
    scene[2].overridesMaterial = true;
    scene[2].color = glm::vec3(1, 0, 0);
    CHECK(edited(scene, selected, undo, redo, 2, before));
    CHECK(undo.size() == 3);

    // Back through them, last first; each selects the object it changed so it can be seen.
    CHECK(undoStep(scene, selected, undo, redo));
    CHECK(scene[2].meshGUID.empty() && !scene[2].overridesMesh && scene[2].materialGUID.empty() && !scene[2].overridesMaterial);
    CHECK(scene[2].color == glm::vec3(0.7f) && selected == 2);
    CHECK(undoStep(scene, selected, undo, redo));
    CHECK(scene[1].visible && scene[1].name == "Bee" && selected == 1);
    CHECK(undoStep(scene, selected, undo, redo));
    CHECK(scene[1].name == "B");
    CHECK(!undoStep(scene, selected, undo, redo));  // nothing left
    CHECK(redo.size() == 3);

    // And forward again.
    CHECK(redoStep(scene, selected, undo, redo) && scene[1].name == "Bee" && scene[1].visible);
    CHECK(redoStep(scene, selected, undo, redo) && !scene[1].visible);
    CHECK(redoStep(scene, selected, undo, redo) && scene[2].meshGUID == "mesh-2" && scene[2].overridesMaterial && selected == 2);
    CHECK(redo.empty() && undo.size() == 3);
}

static void testAnEditThatChangesNothingIsNotAStep() {
    std::vector<SceneObject> scene = sceneOf({"A"});
    int selected = 0;
    std::vector<UndoState> undo, redo;
    redo.push_back(UndoState(0, glm::vec3(0), glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1)));  // something to redo
    const SceneObject before = scene[0];
    CHECK(!edited(scene, selected, undo, redo, 0, before));
    CHECK(undo.empty() && redo.size() == 1);  // and the redo history is left alone
    // Typing a name and putting the old one back is no change either.
    SceneObject typed = scene[0];
    scene[0].name = "AB";
    scene[0].name = "A";
    CHECK(!edited(scene, selected, undo, redo, 0, typed));

    // Each thing the panel can change counts.
    SceneObject o = scene[0];
    scene[0].visible = false;
    CHECK(edited(scene, selected, undo, redo, 0, o));
    o = scene[0];
    scene[0].meshGUID = "m";
    CHECK(edited(scene, selected, undo, redo, 0, o));
    o = scene[0];
    scene[0].materialGUID = "m";
    CHECK(edited(scene, selected, undo, redo, 0, o));
    o = scene[0];
    scene[0].overridesMesh = true;
    CHECK(edited(scene, selected, undo, redo, 0, o));
    o = scene[0];
    scene[0].overridesMaterial = true;
    CHECK(edited(scene, selected, undo, redo, 0, o));
    o = scene[0];
    scene[0].color = glm::vec3(0.1f);
    CHECK(edited(scene, selected, undo, redo, 0, o));
    o = scene[0];
    scene[0].scale = glm::vec3(2);
    CHECK(edited(scene, selected, undo, redo, 0, o));
    CHECK(undo.size() == 7);
}

static void testEditsMixedWithAddsAndDeletes() {
    // Rename an object, duplicate it, delete the original: the history walks back through all three, and each step meets the scene it
    // expects (the duplicate's name check relies on the rename being undone only after it).
    std::vector<SceneObject> scene = sceneOf({"A_01", "B_01"});
    int selected = 0;
    std::vector<UndoState> undo, redo;
    SceneObject before = scene[0];
    scene[0].name = "Hero_01";
    CHECK(edited(scene, selected, undo, redo, 0, before));
    duplicate(scene, selected, undo, redo, 0);
    CHECK(scene[2].name == "Hero_02");
    remove(scene, selected, undo, redo, 0);
    CHECK(describe(scene) == "B_01,Hero_02,");

    CHECK(undoStep(scene, selected, undo, redo) && describe(scene) == "Hero_01,B_01,Hero_02,");
    CHECK(undoStep(scene, selected, undo, redo) && describe(scene) == "Hero_01,B_01,");
    CHECK(undoStep(scene, selected, undo, redo) && describe(scene) == "A_01,B_01,");
    CHECK(redoStep(scene, selected, undo, redo) && describe(scene) == "Hero_01,B_01,");
    CHECK(redoStep(scene, selected, undo, redo) && describe(scene) == "Hero_01,B_01,Hero_02,");
    CHECK(redoStep(scene, selected, undo, redo) && describe(scene) == "B_01,Hero_02,");
}

static void testAStaleEditChangesNothing() {
    std::vector<SceneObject> scene = sceneOf({"A"});
    int selected = 0;
    std::vector<UndoState> undo, redo;
    SceneObject before = scene[0];
    scene[0].visible = false;
    CHECK(edited(scene, selected, undo, redo, 0, before));
    scene.clear();  // the object is gone
    CHECK(!undoStep(scene, selected, undo, redo) && scene.empty() && redo.empty());
}

int main() {
    testNames();
        testCopy();
    testUndoAndRedo();
    testSeveralInARow();
    testNothingSelectedBefore();
    testAStaleStepChangesNothing();
    testTheHistoryIsLimited();
    testDeleteAndUndo();
    testEarlierStepsStillFitAfterADeleteIsUndone();
    testDeletingAfterAnUndoEndsTheRedoHistory();
    testStaleRemovals();
    testEditsUndoAndRedo();
    testAnEditThatChangesNothingIsNotAStep();
    testEditsMixedWithAddsAndDeletes();
    testAStaleEditChangesNothing();
    testAgainstAModel();
    if (failures == 0) {
        std::printf("SceneHistoryTest: all passed\n");
        return 0;
    }
    std::printf("SceneHistoryTest: %d failure(s)\n", failures);
    return 1;
}
