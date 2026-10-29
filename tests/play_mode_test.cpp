// Tests for scripts and the physics together, through the real interpreter (propulsion_ffi.dll): a script's `trace` is
// answered from the scene's shapes, and `when Self touches` / `stops touching` / `hits` fire when the physics says so.
// Scripts are written to a temporary project folder and run by PlayMode, the way the editor runs them.
//
//   cmake --build . --target PlayModeTest --config Debug
//   build/tests/PlayModeTest.exe

#include "Engine.h"  // (SceneObject)
#include "physics/PlayPhysics.h"
#include "input/DefaultInput.h"
#include "scripting/PlayMode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

namespace fs = std::filesystem;

static void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

static int count(const std::vector<std::string>& lines, const std::string& text) {
    return static_cast<int>(std::count(lines.begin(), lines.end(), text));  // the log holds the bare messages: the category says they are the script's
}

/// A scene and the physics that goes with it, stepped the way Engine::stepPlayPhysics does.
struct Level {
    std::vector<SceneObject> scene;
    std::vector<Physics::Body> bodies;
    Physics::Contacts contacts;
    std::unique_ptr<PlayMode> play;
    std::vector<std::string> log;
    std::vector<std::string> typedCategories;  // parallel to `typed`
    std::vector<LogEntry> typed;  // the same messages with the category and level they were logged at

    // A scene object whose mesh spans -1..1 (like the default meshes), so its scale is its half-size.
    size_t add(const std::string& name, glm::vec3 position, glm::vec3 scale, const std::string& script = "", bool solid = true,
               const std::string& inputMapping = "") {
        SceneObject obj(name);
        obj.position = position;
        obj.scale = scale;
        obj.scriptFile = script;
        obj.inputMappingId = inputMapping;
        scene.push_back(obj);
        Physics::Body b;
        b.hasShape = true;
        b.solid = solid;
        b.boundsMin = glm::vec3(-1.0f);
        b.boundsMax = glm::vec3(1.0f);
        b.scripted = !script.empty();
        bodies.push_back(b);
        return scene.size() - 1;
    }
    void makeCharacter(size_t i) {
        bodies[i].character = true;
        bodies[i].movement.enabled = true;
        bodies[i].state.position = scene[i].position;
    }
    std::vector<Physics::Pose> poses() const {
        std::vector<Physics::Pose> p(scene.size());
        for (size_t i = 0; i < scene.size(); ++i) {
            p[i].position = scene[i].position;
            p[i].rotation = scene[i].quaternion;
            p[i].scale = scene[i].scale;
            p[i].collisionEnabled = scene[i].collisionEnabled;
        }
        return p;
    }
    bool start(const std::string& projectDir, const InputSetup& input = InputSetup()) {
        play = std::make_unique<PlayMode>(nullptr, [this](const char* category, LogLevel level, const std::string& line) {
            log.push_back(line);
            LogEntry entry;
            entry.level = level;
            entry.message = line;
            typed.push_back(entry);
            typedCategories.push_back(category);
        });
        PlayMode::setScriptInputs(input.actions);
        play->setInput(input);
        play->setTraceProvider([this](int asker, const float* from, const float* to, float radius, PlayMode::TraceResult& out) {
            const Physics::TraceHit hit = Physics::traceBodies(bodies, poses(), asker, glm::vec3(from[0], from[1], from[2]),
                                                               glm::vec3(to[0], to[1], to[2]), radius);
            out.blocked = hit.blocked;
            out.sceneIndex = hit.blocked ? hit.id : -1;
            for (int i = 0; i < 3; ++i) {
                out.location[i] = hit.location[i];
                out.normal[i] = hit.normal[i];
            }
            out.distance = hit.distance;
            return true;
        });
        return play->start(scene, projectDir);
    }
    void frame(float dt = 1.0f / 60.0f, bool allowInput = true) {
        const float advanced = play->step(dt, scene, allowInput);
        std::vector<Physics::Pose> p = poses();
        std::vector<size_t> moved;
        Physics::stepBodies(bodies, p, advanced,
                            [this](size_t body, const std::string& action) { return play->actionStartedThisStep(body, action); }, &moved, &contacts);
        for (size_t i : moved) {
            scene[i].position = p[i].position;
            play->setPosition(i, p[i].position.x, p[i].position.y, p[i].position.z);
        }
        for (const auto& pair : contacts.began) play->reportTouch(pair.first, pair.second, true);
        for (const auto& pair : contacts.ended) play->reportTouch(pair.first, pair.second, false);
        for (const auto& pair : contacts.hit) play->reportHit(pair.first, pair.second);
    }
};

static const char* kTracer =
    "// looks along a line once when created and says what it saw\n"
    "\n"
    "header:\n"
    "\tneeds component:Transform\n"
    "\n"
    "implement:\n"
    "\twhen created:\n"
    "\t\tremember Start vector x:3 y:0 z:1\n"
    "\t\tremember End vector x:3 y:20 z:1\n"
    "\t\tremember Seen trace from Start to End\n"
    "\t\tprint \"blocked \" joined with Seen.Blocked\n"
    "\t\tprint \"far \" joined with Seen.Distance\n"
    "\t\tif Seen.Entity is missing, then print \"nobody\"\n"
    "\t\tif Seen.Entity is present, then print \"somebody\"\n";

static const char* kLooksDown =
    "// looks straight down once when created\n"
    "\n"
    "header:\n"
    "\tneeds component:Transform\n"
    "\n"
    "implement:\n"
    "\twhen created:\n"
    "\t\tremember Start vector x:0 y:-8 z:1\n"
    "\t\tremember End vector x:0 y:-8 z:-20\n"
    "\t\tremember Seen trace from Start to End\n"
    "\t\tprint \"down \" joined with Seen.Distance\n"
    "\t\tif Seen.Entity is missing, then print \"floor has no script\"\n";

static const char* kSelfCheck =
    "// a solid object that traces from its own location: it must not find itself\n"
    "\n"
    "header:\n"
    "\tneeds component:Transform\n"
    "\n"
    "implement:\n"
    "\twhen created:\n"
    "\t\tremember End vector x:30 y:20 z:1\n"
    "\t\tremember Seen trace from Transform.Loc to End\n"
    "\t\tif Seen.Blocked is true, then print \"found itself\"\n"
    "\t\tif Seen.Blocked is false, then print \"clear\"\n";

static const char* kWall =
    "// a wall with a script, so a script can say it hit it\n"
    "\n"
    "header:\n"
    "\tneeds component:Transform\n"
    "\n"
    "implement:\n"
    "\twhen created:\n"
    "\t\tprint \"wall is here\"\n";

static const char* kWalker =
    "// walks forward, and says when it hit the wall\n"
    "\n"
    "header:\n"
    "\tneeds component:Transform\n"
    "\tneeds file:\"wall.prop\" as Wall\n"
    "\n"
    "implement:\n"
    "\tevery frame:\n"
    "\t\tmove Self forward by 5 per second\n"
    "\n"
    "\twhen Self hits Wall as Thing:\n"
    "\t\tprint \"walker hit the wall\"\n";

static const char* kCoin =
    "// says when the walker touches it, and when it stops\n"
    "\n"
    "header:\n"
    "\tneeds component:Transform\n"
    "\tneeds file:\"walker.prop\" as Walker\n"
    "\n"
    "implement:\n"
    "\twhen Self touches Walker as Visitor:\n"
    "\t\tprint \"coin touched\"\n"
    "\n"
    "\twhen Self stops touching Walker as Visitor:\n"
    "\t\tprint \"coin released\"\n";

static void testScriptsAndPhysicsTogether() {
    const fs::path dir = fs::temp_directory_path() / "propulsion_play_mode_test";
    fs::remove_all(dir);
    write(dir / "Scripts" / "tracer.prop", kTracer);
    write(dir / "Scripts" / "lookdown.prop", kLooksDown);
    write(dir / "Scripts" / "selfcheck.prop", kSelfCheck);
    write(dir / "Scripts" / "wall.prop", kWall);
    write(dir / "Scripts" / "walker.prop", kWalker);
    write(dir / "Scripts" / "coin.prop", kCoin);

    Level level;
    level.add("Floor", glm::vec3(0, 0, -0.5f), glm::vec3(50, 50, 0.5f));                                    // no script: only the engine knows it
    level.add("Wall", glm::vec3(0, 5, 1), glm::vec3(10, 0.5f, 2), "Scripts/wall.prop");                   // its near face is at y = 4.5
    const size_t tracer = level.add("Tracer", glm::vec3(-20, 0, 1), glm::vec3(0.1f), "Scripts/tracer.prop", false);
    const size_t lookdown = level.add("LookDown", glm::vec3(-20, 0, 1), glm::vec3(0.1f), "Scripts/lookdown.prop", false);
    const size_t coin = level.add("Coin", glm::vec3(0, 2, 1), glm::vec3(0.5f), "Scripts/coin.prop", false);  // a ball, not solid
    const size_t walker = level.add("Walker", glm::vec3(0, 0, 1), glm::vec3(1.0f), "Scripts/walker.prop");
    level.makeCharacter(walker);
    level.add("Crate", glm::vec3(30, 0, 1), glm::vec3(1.0f), "Scripts/selfcheck.prop");  // solid, and traces from inside itself
    (void)tracer;
    (void)lookdown;
    (void)coin;

    CHECK(level.start(dir.generic_string()));

    // Scripts' traces were answered from the shapes, and the scene index of what was hit was turned into the script
    // entity it is (the wall is entity 0: the floor, which has no script, comes first in the scene but has no entity).
    CHECK(count(level.log, "blocked true") == 1 && count(level.log, "far 4.5") == 1);  // the wall, along y at x = 3
    CHECK(count(level.log, "somebody") == 1);                                            // the wall has a script, so it can be named
    CHECK(count(level.log, "down 1") == 1 && count(level.log, "floor has no script") == 1);  // the floor can't

    CHECK(count(level.log, "clear") == 1 && count(level.log, "found itself") == 0);  // a trace skips the one that made it

    // Eight seconds of the script walking forward at 5 a second: 40 units if nothing stopped it. The wall does, and the
    // script is told where the physics left it each frame, so it never builds up a 20-unit "move" that would count as a teleport.
    for (int i = 0; i < 480; ++i) level.frame();

    CHECK(std::fabs(level.scene[walker].position.y - 4.0f) < 0.02f);  // the physics stopped it at the wall: 4.5 minus its radius
    CHECK(std::fabs(level.scene[walker].position.z - 1.0f) < 0.02f);
    CHECK(count(level.log, "walker hit the wall") == 1);                // once
    CHECK(count(level.log, "coin touched") == 1);                       // it walked into the coin ...
    CHECK(count(level.log, "coin released") == 1);                      // ... and out the other side (the coin isn't solid)

    fs::remove_all(dir);
}

static const char* kMover =
    "// walks forward as its own forward input says, and says hello when greet starts\n"
    "\n"
    "header:\n"
    "\tneeds component:Transform\n"
    "\tneeds system:Input\n"
    "\n"
    "implement:\n"
    "\tevery frame:\n"
    "\t\tremember Forward axis moveForward of Input\n"
    "\t\tremember Amount Forward times 5.0\n"
    "\t\tmove Self forward by Amount per second\n"
    "\t\tif Input is pressing greet, then add 1.0 per second to Transform.Loc.X\n"
    "\t\tif Input is not pressing greet, then add 2.0 per second to Transform.Loc.X\n"
    "\n"
    "\twhen Input starts pressing greet:\n"
    "\t\tprint \"hello\"\n"
    "\n"
    "\twhen Input stops pressing greet:\n"
    "\t\tprint \"goodbye\"\n";

static void testInputComesFromEachObjectsOwnMapping() {
    const fs::path dir = fs::temp_directory_path() / "propulsion_play_mode_input_test";
    fs::remove_all(dir);
    write(dir / "Scripts" / "mover.prop", kMover);

    // The project's input: three actions, and two tables of keys.
    InputSetup input;
    input.actions = {{"A-forward", "moveForward", InputValueType::Axis, ""}, {"A-greet", "greet", InputValueType::Button, ""}, {"A-jump", "jump", InputValueType::Button, ""}};
    input.mappings = {
        {"M-wasd", "Wasd", {{"A-forward", "W", false, 1.0f}, {"A-forward", "S", true, 1.0f}, {"A-greet", "F", false, 1.0f}}},
        {"M-ijkl", "Ijkl", {{"A-forward", "I", false, 1.0f}, {"A-jump", "Space", false, 1.0f}}},
    };

    Level level;
    level.add("Floor", glm::vec3(0, 0, -0.5f), glm::vec3(100, 100, 0.5f));
    const size_t one = level.add("One", glm::vec3(-10, 0, 1), glm::vec3(1.0f), "Scripts/mover.prop", true, "M-wasd");
    const size_t two = level.add("Two", glm::vec3(0, 0, 1), glm::vec3(1.0f), "Scripts/mover.prop", true, "M-ijkl");
    const size_t three = level.add("Three", glm::vec3(10, 0, 1), glm::vec3(1.0f), "Scripts/mover.prop", true, "");  // no mapping
    for (size_t i : {one, two, three}) level.makeCharacter(i);
    level.bodies[one].movement.jumpAction = "jump";   // its mapping has no `jump` row
    level.bodies[two].movement.jumpAction = "jump";
    level.bodies[three].movement.jumpAction = "jump";

    CHECK(level.start(dir.generic_string(), input));  // the scripts are checked against these actions: `greet` isn't a built-in name
    auto settle = [&](int frames) { for (int i = 0; i < frames; ++i) level.frame(1.0f / 60.0f); };
    settle(30);

    // Holding W (bound to moveForward in the first table only) walks only the first player.
    level.play->injectedInput().down = {"W"};
    settle(60);
    CHECK(std::fabs(level.scene[one].position.y - 5.0f) < 0.1f);
    CHECK(std::fabs(level.scene[two].position.y) < 0.001f && std::fabs(level.scene[three].position.y) < 0.001f);

    // Holding I walks only the second; S (negated) walks the first back.
    level.play->injectedInput().down = {"I", "S"};
    settle(60);
    CHECK(std::fabs(level.scene[two].position.y - 5.0f) < 0.1f);
    CHECK(std::fabs(level.scene[one].position.y) < 0.1f);  // 5 forward then 5 back
    CHECK(std::fabs(level.scene[three].position.y) < 0.001f);

    // A button goes down for one step however long it is held: the first player says hello once, as it goes down, and
    // goodbye once, as it comes up. Held, it is "pressing"; for the third player, whose input has no mapping, it never is.
    const float oneX = level.scene[one].position.x, twoX = level.scene[two].position.x, threeX = level.scene[three].position.x;
    level.play->injectedInput().down = {"F"};
    settle(60);
    CHECK(count(level.log, "hello") == 1 && count(level.log, "goodbye") == 0);   // down for a second: only the start has happened
    CHECK(std::fabs((level.scene[one].position.x - oneX) - 1.0f) < 0.1f);        // `is pressing`: 1 a second
    CHECK(std::fabs((level.scene[two].position.x - twoX) - 2.0f) < 0.1f);        // `is not pressing` (it has no `greet`): 2 a second
    CHECK(std::fabs((level.scene[three].position.x - threeX) - 2.0f) < 0.1f);    // and with no mapping at all, nothing is ever pressed
    level.play->injectedInput().down = {};
    settle(5);
    CHECK(count(level.log, "hello") == 1 && count(level.log, "goodbye") == 1);

    // Space is the second player's jump only: the first has no Space row, the third has no mapping at all.
    level.play->injectedInput().down = {"Space"};
    float apex[3] = {0, 0, 0};
    for (int i = 0; i < 60; ++i) {
        level.frame(1.0f / 60.0f);
        level.play->injectedInput().down = {};
        apex[0] = (std::max)(apex[0], level.scene[one].position.z);
        apex[1] = (std::max)(apex[1], level.scene[two].position.z);
        apex[2] = (std::max)(apex[2], level.scene[three].position.z);
    }
    CHECK(apex[1] > 2.0f);
    CHECK(apex[0] < 1.05f && apex[2] < 1.05f);

    // While the user is typing into an editor text field, the keys aren't the player's: nothing is read.
    const float beforeTyping = level.scene[one].position.y;
    level.play->injectedInput().down = {"W"};
    for (int i = 0; i < 30; ++i) level.frame(1.0f / 60.0f, false);
    CHECK(std::fabs(level.scene[one].position.y - beforeTyping) < 0.001f);
    level.play->injectedInput().down = {};
    settle(2);

    // Rebinding is just editing the table: the second player's jump moves to J, and Space does nothing any more.
    input.mappings[1].rows[1].key = "J";
    level.play->setInput(input);
    auto jumpWith = [&](const char* key) {
        float top = 0.0f;
        level.play->injectedInput().down = {key};
        for (int i = 0; i < 60; ++i) {
            level.frame(1.0f / 60.0f);
            level.play->injectedInput().down = {};
            top = (std::max)(top, level.scene[two].position.z);
        }
        return top;
    };
    CHECK(jumpWith("Space") < 1.05f);
    CHECK(jumpWith("J") > 2.0f);
    fs::remove_all(dir);
}

// The finished player script (PropulsionScript/behaviors/player.prop) on a Character Movement body, with the default input: the
// script walks and reports the jump, the engine does the standing, falling and jumping.
static void testTheFinishedPlayerScript() {
    const fs::path dir = fs::temp_directory_path() / "propulsion_play_mode_player_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "Scripts");
    fs::copy_file(fs::path(PROPULSION_BEHAVIORS_DIR) / "player.prop", dir / "Scripts" / "player.prop");

    int next = 0;
    InputSetup input = makeDefaultInput([&] { return "G" + std::to_string(next++); });

    Level level;
    level.add("Floor", glm::vec3(0, 0, -0.5f), glm::vec3(100, 100, 0.5f));
    const size_t player = level.add("Player", glm::vec3(0, 0, 1), glm::vec3(1.0f), "Scripts/player.prop", true, input.mappings[0].id);
    level.makeCharacter(player);
    level.bodies[player].movement.jumpAction = "jump";

    CHECK(level.start(dir.generic_string(), input));
    auto settle = [&](int frames) { for (int i = 0; i < frames; ++i) level.frame(1.0f / 60.0f); };

    // Standing still, it stays on the floor.
    settle(60);
    CHECK(std::fabs(level.scene[player].position.z - 1.0f) < 0.02f);
    CHECK(std::fabs(level.scene[player].position.y) < 0.001f);

    // W is forward (+Y) at the script's speed, and walking stays on the floor.
    level.play->injectedInput().down = {"W"};
    settle(60);
    CHECK(std::fabs(level.scene[player].position.y - 5.0f) < 0.2f);
    CHECK(std::fabs(level.scene[player].position.z - 1.0f) < 0.02f);
    level.play->injectedInput().down = {};
    settle(10);

    // Space: the engine lifts it (the script no longer does), and the script counts the press and says so, once.
    float top = 0.0f;
    level.play->injectedInput().down = {"Space"};
    for (int i = 0; i < 90; ++i) {
        level.frame(1.0f / 60.0f);
        level.play->injectedInput().down = {};
        top = (std::max)(top, level.scene[player].position.z);
    }
    CHECK(top > 2.0f && top < 3.0f);                                  // 1.6 above where it stood: not the old +2 hop
    CHECK(std::fabs(level.scene[player].position.z - 1.0f) < 0.02f);  // and back on the floor
    CHECK(count(level.log, "Jumped 1") == 1 && count(level.log, "Jumped 2") == 0);

    fs::remove_all(dir);
}

/// A script with a mistake stops Play from starting, and what it says is logged as errors in the compiler's category (so the
/// Output Log shows them red, and can filter them).
static void testAScriptWithAMistakeIsLoggedAsErrors() {
    const fs::path dir = fs::temp_directory_path() / "propulsion_play_mode_errors_test";
    fs::remove_all(dir);
    write(dir / "Scripts" / "broken.prop", "implement:\n\twhen created:\n\t\tthis is not a statement\n");

    Level level;
    level.add("Broken", glm::vec3(0), glm::vec3(1.0f), "Scripts/broken.prop");
    CHECK(!level.start(dir.generic_string()));  // nothing was started

    CHECK(level.typed.size() >= 2);
    bool anyCompileError = false, allErrors = !level.typed.empty(), namesTheFile = false;
    for (size_t i = 0; i < level.typed.size(); ++i) {
        anyCompileError |= level.typedCategories[i] == LogCategory::Compile;
        allErrors &= level.typed[i].level == LogLevel::Error;
        namesTheFile |= level.typed[i].message.find("broken.prop") != std::string::npos;  // the "--> file:line:col" row
    }
    CHECK(anyCompileError);
    CHECK(allErrors);
    CHECK(namesTheFile);

    fs::remove_all(dir);
}

int main() {
    testAScriptWithAMistakeIsLoggedAsErrors();
    testScriptsAndPhysicsTogether();
    testTheFinishedPlayerScript();
    testInputComesFromEachObjectsOwnMapping();
    if (failures == 0) {
        std::printf("PlayModeTest: all passed\n");
        return 0;
    }
    std::printf("PlayModeTest: %d failure(s)\n", failures);
    return 1;
}
