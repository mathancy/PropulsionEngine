#pragma once

/**
 * PlayMode.h
 * Runs ForgeScript (.forge) scripts on the scene while the editor is in Play mode.
 *
 * Scripts are plain text files in the project's Scripts folder. They are read when Play starts and
 * run by the ForgeScript interpreter (forge_ffi.dll), so changing a script never needs a rebuild:
 * edit it, save, press Play. (Packaged games will use C++ generated from the same scripts.)
 *
 * Play mode never edits the scene directly: the Engine keeps a snapshot, PlayMode copies each
 * scripted object's transform out of its script every frame, and stopping restores the snapshot.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Forge {

struct SceneObject;

class PlayMode {
public:
    using Log = std::function<void(const std::string&)>;

    /// `nativeWindow` is the GLFWwindow keyboard input is read from; `log` receives script output.
    PlayMode(void* nativeWindow, Log log);
    ~PlayMode();

    /// Load every script the scene's objects use (`SceneObject::scriptFile`, relative to `projectDir`)
    /// and start one for each such object. Returns false if a script has a mistake or can't be read:
    /// the problems, with their line numbers, are in the log, and nothing is running.
    bool start(const std::vector<SceneObject>& scene, const std::string& projectDir);

    /// How many scene objects are running a script.
    int runningCount() const;

    /// Advance the scripts by `deltaTime` seconds and write their changes back into `scene`.
    /// `allowInput` is false while the user is typing into an editor text field.
    void step(float deltaTime, std::vector<SceneObject>& scene, bool allowInput);

    // ---- script files (no Play session needed) ----

    /// Every .forge file under `<projectDir>/Scripts`, as paths relative to `projectDir`
    /// with forward slashes (`Scripts/Door.forge`).
    static std::vector<std::string> scriptsIn(const std::string& projectDir);

    /// Create `Scripts/<name>.forge` with a short starting script (a numbered name if it's taken).
    /// Returns its path relative to `projectDir`, or an empty string with `error` set.
    static std::string createScript(const std::string& projectDir, const std::string& name, std::string& error);

    /// Check a script file without running it. Returns the problems (with line numbers), or an
    /// empty string if it is fine.
    static std::string checkScript(const std::string& absolutePath);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Forge
