#pragma once

/**
 * PlayMode.h
 * Runs PropulsionScript (.prop) scripts on the scene while the editor is in Play mode.
 *
 * Scripts are plain text files in the project's Scripts folder. They are read when Play starts and
 * run by the PropulsionScript interpreter (propulsion_ffi.dll), so changing a script never needs a rebuild:
 * edit it, save, press Play. (Packaged games will use C++ generated from the same scripts.)
 *
 * Play mode never edits the scene directly: the Engine keeps a snapshot, PlayMode copies each
 * scripted object's transform out of its script every frame, and stopping restores the snapshot.
 */

#include "core/OutputLog.h"
#include "input/InputMap.h"
#include "input/InputTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Propulsion {

struct SceneObject;

class PlayMode {
public:
    using Log = LogFn;

    /// `nativeWindow` is the GLFWwindow keyboard input is read from; `log` receives script output (print, warnings,
    /// and the compiler's problems), each with a category and a level.
    PlayMode(void* nativeWindow, Log log);
    ~PlayMode();

    /// Load every script the scene's objects use (`SceneObject::scriptFile`, relative to `projectDir`)
    /// and start one for each such object. Returns false if a script has a mistake or can't be read:
    /// the problems, with their line numbers, are in the log, and nothing is running.
    bool start(const std::vector<SceneObject>& scene, const std::string& projectDir);

    /// Capture the mouse: the cursor is hidden and its movement becomes the `lookX` / `lookY` axes
    /// (see MouseLook.h). It is handed back by `setMouseCaptured(false)` and whenever Play ends, and
    /// while the window is out of focus.
    void setMouseCaptured(bool captured);
    bool mouseCaptured() const;

    /// How many scene objects are running a script.
    int runningCount() const;

    /// Advance the scripts by `deltaTime` seconds and write their changes back into `scene`.
    /// `allowInput` is false while the user is typing into an editor text field.
    /// Returns how much time the game moved on by: `deltaTime`, but never more than a tenth of a second at once, and
    /// nothing while a script has paused the game. (The physics advances by the same amount.)
    float step(float deltaTime, std::vector<SceneObject>& scene, bool allowInput);

    /// What a script's `trace` found, in the engine's terms: scene objects, not script entities.
    struct TraceResult {
        bool blocked = false;
        int sceneIndex = -1;     // the scene object it hit, or -1 for nothing
        float location[3] = {0, 0, 0};
        float normal[3] = {0, 0, 0};
        float distance = 0.0f;
    };
    /// Answers a script's `trace` / `sweep`: what the line from `from` to `to` (a ball of `radius` slid along it) meets
    /// first, skipping scene object `fromSceneIndex`, the one that asked (-1 if unknown). Return false to have the scripts
    /// guess instead (each scripted object a ball). Without a provider they guess.
    using TraceProvider = std::function<bool(int fromSceneIndex, const float* from, const float* to, float radius, TraceResult& out)>;
    void setTraceProvider(TraceProvider provider);

    /// Tell the scripts two scene objects began (or stopped) touching, for `when Self touches ...` and
    /// `when Self stops touching ...`, or that one hit the other, for `when Self hits ...`. Objects with no script are ignored.
    void reportTouch(size_t sceneIndexA, size_t sceneIndexB, bool touching);
    void reportHit(size_t sceneIndexA, size_t sceneIndexB);

    /// The project's input: its Input Actions, and its Input Mappings (the tables of keys). Call before `start`. Each
    /// scripted object is played with the mapping its Object names (`SceneObject::inputMappingId`); an object with none
    /// gets no input.
    void setInput(InputSetup setup);

    /// Tell the script checker which Input Actions the project has, so a script reading an action that doesn't exist is
    /// refused (and the graph editor offers the right names). Call before scripts are checked or loaded, and again
    /// whenever the actions change.
    static void setScriptInputs(const std::vector<InputActionDef>& actions);

    /// Whether the Input Action `action` went down during the last `step`, in the input of scene object `sceneIndex`.
    bool actionStartedThisStep(size_t sceneIndex, const std::string& action) const;

    /// What the player is doing, for a PlayMode with no window to read (tests): fill this in, then `step` with input allowed.
    InputState& injectedInput();

    /// Put a scripted object back where the physics left it, so its script carries on from there. Only the location
    /// changes: its rotation and scale stay what the script has. Does nothing for an object that isn't running a script.
    void setPosition(size_t sceneIndex, float x, float y, float z);

    // ---- script files (no Play session needed) ----

    /// Every .prop file under `<projectDir>/Scripts`, as paths relative to `projectDir`
    /// with forward slashes (`Scripts/Door.prop`).
    static std::vector<std::string> scriptsIn(const std::string& projectDir);

    /// Create `Scripts/<name>.prop` with a short starting script (a numbered name if it's taken).
    /// Returns its path relative to `projectDir`, or an empty string with `error` set.
    static std::string createScript(const std::string& projectDir, const std::string& name, std::string& error);

    /// Check a script file without running it. Returns the problems (with line numbers), or an
    /// empty string if it is fine.
    static std::string checkScript(const std::string& absolutePath);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Propulsion
