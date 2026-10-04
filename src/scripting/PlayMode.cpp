// Standard headers first: some engine headers pull in <windows.h>, whose min/max macros break them.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "scripting/PlayMode.h"

#include "Engine.h"

#include "forge_ffi.h"

#include <GLFW/glfw3.h>

namespace Forge {

namespace fs = std::filesystem;

namespace {

struct Binding {
    size_t sceneIndex;
    int64_t entity;
};

/// What scripts can ask of the engine. Input comes from the keyboard; there is no audio or UI
/// system yet, so calls on those are written to the log instead.
struct EngineHost {
    GLFWwindow* window = nullptr;
    PlayMode::Log log;
    std::set<std::string> held, started, stopped;
    /// `do SetPaused on Game paused:true`: while set, the scripts don't advance.
    bool paused = false;
    /// What `level name` says. The engine has one scene at a time, so it is the project's name.
    std::string levelName;

    static const std::map<std::string, std::vector<int>>& keyMap() {
        static const std::map<std::string, std::vector<int>> keys = {
            {"moveForward", {GLFW_KEY_W, GLFW_KEY_UP}},
            {"moveBack", {GLFW_KEY_S, GLFW_KEY_DOWN}},
            {"moveLeft", {GLFW_KEY_A, GLFW_KEY_LEFT}},
            {"moveRight", {GLFW_KEY_D, GLFW_KEY_RIGHT}},
            {"jump", {GLFW_KEY_SPACE}},
            {"crouch", {GLFW_KEY_LEFT_CONTROL}},
            {"interact", {GLFW_KEY_E}},
        };
        return keys;
    }

    /// Read the keyboard once per frame, so "starts pressing" fires on exactly one step.
    void poll(bool allowInput) {
        std::set<std::string> now;
        if (allowInput && window) {
            for (const auto& [input, keys] : keyMap()) {
                for (int key : keys) {
                    if (glfwGetKey(window, key) == GLFW_PRESS) {
                        now.insert(input);
                        break;
                    }
                }
            }
        }
        started.clear();
        stopped.clear();
        for (const auto& input : now) {
            if (!held.count(input)) started.insert(input);
        }
        for (const auto& input : held) {
            if (!now.count(input)) stopped.insert(input);
        }
        held = std::move(now);
    }

    // ---- callbacks the interpreter makes (see forge_ffi.h) ----

    static bool pressing(void* user, const char* system, const char* input, int32_t kind) {
        auto* self = static_cast<EngineHost*>(user);
        if (std::string(system) != "Input") return kind == 1;
        switch (kind) {
            case 0: return self->held.count(input) > 0;
            case 1: return self->held.count(input) == 0;
            case 2: return self->started.count(input) > 0;
            default: return self->stopped.count(input) > 0;
        }
    }

    static float axis(void* user, const char* system, const char* input) {
        auto* self = static_cast<EngineHost*>(user);
        if (std::string(system) != "Input") return 0.0f;
        auto down = [&](const char* name) { return self->held.count(name) > 0 ? 1.0f : 0.0f; };
        std::string which = input;
        if (which == "moveForward") return down("moveForward") - down("moveBack");
        if (which == "moveRight") return down("moveRight") - down("moveLeft");
        return 0.0f;  // lookX / lookY need mouse capture, which Play mode doesn't take yet
    }

    static void call(void* user, const char* system, const char* action, const char* args) {
        auto* self = static_cast<EngineHost*>(user);
        // Pausing is something the editor can really do: the scripts stop advancing.
        if (std::string(system) == "Game" && std::string(action) == "SetPaused") {
            self->paused = std::string(args).find("paused=true") != std::string::npos;
            self->log(std::string("[script] the game is ") + (self->paused ? "paused" : "running again"));
            return;
        }
        self->log(std::string("[script] ") + system + "." + action + (*args ? " " : "") + args +
                  "  (not connected to an engine system yet)");
    }

    static const char* levelNameOf(void* user) {
        return static_cast<EngineHost*>(user)->levelName.c_str();
    }

    static void print(void* user, const char* text) {
        static_cast<EngineHost*>(user)->log(std::string("[script] ") + text);
    }

    static void endFrame(void* user) {
        auto* self = static_cast<EngineHost*>(user);
        self->started.clear();
        self->stopped.clear();
    }
};

/// A path the interpreter and the engine will agree on.
std::string absolutePath(const fs::path& projectDir, const std::string& relative) {
    return (projectDir / relative).lexically_normal().generic_string();
}

/// Send each line of `text` to the log, with a prefix.
void logLines(const PlayMode::Log& log, const std::string& prefix, const std::string& text) {
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty()) log(prefix + line);
        start = end + 1;
    }
}

} // namespace

struct PlayMode::Impl {
    EngineHost host;
    Log log;
    ForgeSession* session = nullptr;
    std::vector<Binding> bindings;
    size_t spawnedByHost = 0;
    bool reportedScriptSpawns = false;

    ~Impl() {
        if (session) forge_session_free(session);
    }
};

PlayMode::PlayMode(void* nativeWindow, Log log) : m_impl(std::make_unique<Impl>()) {
    m_impl->host.window = static_cast<GLFWwindow*>(nativeWindow);
    m_impl->host.log = log;
    m_impl->log = std::move(log);
}
PlayMode::~PlayMode() = default;

bool PlayMode::start(const std::vector<SceneObject>& scene, const std::string& projectDir) {
    Impl& s = *m_impl;

    // The scripts this scene needs, each loaded once.
    std::vector<std::string> paths;
    for (const SceneObject& obj : scene) {
        if (obj.scriptFile.empty()) continue;
        std::string abs = absolutePath(projectDir, obj.scriptFile);
        if (std::find(paths.begin(), paths.end(), abs) == paths.end()) paths.push_back(abs);
    }
    if (paths.empty()) return true;  // nothing to run: objects without scripts just exist

    ForgeHost host{};
    host.user = &s.host;
    host.pressing = &EngineHost::pressing;
    host.axis = &EngineHost::axis;
    host.call = &EngineHost::call;
    host.print = &EngineHost::print;
    host.end_frame = &EngineHost::endFrame;
    host.level_name = &EngineHost::levelNameOf;
    s.host.levelName = fs::path(projectDir).filename().string();

    std::vector<const char*> cpaths;
    for (const std::string& p : paths) cpaths.push_back(p.c_str());
    char* errors = nullptr;
    s.session = forge_session_new(host, cpaths.data(), cpaths.size(), &errors);
    if (!s.session) {
        s.log("[script] ERROR: a script has a mistake, so nothing was started:");
        if (errors) logLines(s.log, "[script]   ", errors);
        forge_string_free(errors);
        return false;
    }

    for (size_t i = 0; i < scene.size(); ++i) {
        const SceneObject& source = scene[i];
        if (source.scriptFile.empty()) continue;
        std::string abs = absolutePath(projectDir, source.scriptFile);
        int64_t id = forge_spawn(s.session, abs.c_str(), &source.position.x, &source.rotation.x, &source.scale.x);
        if (id < 0) {
            s.log("[script] ERROR: could not start " + source.scriptFile + " for " + source.name);
            continue;
        }
        s.bindings.push_back({i, id});
        s.log("[script] " + source.name + " is running " + source.scriptFile);
    }
    s.spawnedByHost = forge_entity_count(s.session);
    return true;
}

int PlayMode::runningCount() const {
    return static_cast<int>(m_impl->bindings.size());
}

void PlayMode::step(float deltaTime, std::vector<SceneObject>& scene, bool allowInput) {
    Impl& s = *m_impl;
    if (!s.session) return;

    s.host.poll(allowInput);
    // A long hitch (a breakpoint, dragging the window) shouldn't throw an object across the level.
    // (While the game is paused the scripts stand still, and the objects stay as they are.)
    if (!s.host.paused) forge_step(s.session, (std::min)(deltaTime, 0.1f));

    if (char* warnings = forge_take_warnings(s.session)) {
        logLines(s.log, "[script] warning: ", warnings);
        forge_string_free(warnings);
    }

    for (const Binding& b : s.bindings) {
        if (b.sceneIndex >= scene.size()) continue;
        SceneObject& target = scene[b.sceneIndex];
        glm::vec3 loc, rot, scl;
        if (forge_get_transform(s.session, b.entity, &loc.x, &rot.x, &scl.x)) {
            target.position = loc;
            target.rotation = rot;
            target.scale = scl;
            // The renderer draws from the quaternion; the editor keeps it in step with the Euler angles.
            target.quaternion = glm::quat(glm::radians(rot));
            // `make Visibility.Shown false` hides the object; collisions aren't detected by the editor yet.
            bool shown = true, collides = true;
            if (forge_get_flags(s.session, b.entity, &shown, &collides)) target.visible = shown;
        } else {
            target.visible = false;  // `destroy Self`
        }
    }

    if (!s.reportedScriptSpawns && forge_entity_count(s.session) > s.spawnedByHost) {
        s.reportedScriptSpawns = true;
        s.log("[script] note: a script spawned new objects. They run, but don't appear in the viewport yet.");
    }
}

std::vector<std::string> PlayMode::scriptsIn(const std::string& projectDir) {
    std::vector<std::string> found;
    std::error_code ec;
    fs::path root = fs::path(projectDir) / "Scripts";
    if (projectDir.empty() || !fs::is_directory(root, ec)) return found;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file() && it->path().extension() == ".forge") {
            found.push_back(fs::relative(it->path(), projectDir).generic_string());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::string PlayMode::createScript(const std::string& projectDir, const std::string& name, std::string& error) {
    if (projectDir.empty()) {
        error = "Save the project first, so there is a folder to put the script in.";
        return "";
    }
    // A file name made of letters, digits, and a few safe characters.
    std::string safe;
    for (char ch : name) {
        bool ok = std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == ' ';
        safe += ok ? ch : '_';
    }
    if (safe.empty()) safe = "Script";

    std::error_code ec;
    fs::path dir = fs::path(projectDir) / "Scripts";
    fs::create_directories(dir, ec);
    if (ec) {
        error = "Could not create the Scripts folder: " + ec.message();
        return "";
    }
    fs::path file = dir / (safe + ".forge");
    for (int n = 2; fs::exists(file, ec); ++n) file = dir / (safe + "_" + std::to_string(n) + ".forge");

    std::ofstream out(file, std::ios::binary);
    if (!out) {
        error = "Could not write " + file.string();
        return "";
    }
    out << "// " << file.filename().string() << ": what " << safe << " does. Edit this file, save it, and press Play.\n"
        << "\n"
        << "header:\n"
        << "\tneeds component:Transform\n"
        << "\n"
        << "implement:\n"
        << "\twhen created:\n"
        << "\t\tprint \"" << safe << " is running\"\n";
    out.close();
    return fs::relative(file, projectDir).generic_string();
}

std::string PlayMode::checkScript(const std::string& absolutePath) {
    char* errors = nullptr;
    bool fine = forge_check_file(absolutePath.c_str(), &errors);
    std::string text = errors ? errors : "";
    forge_string_free(errors);
    return fine ? "" : (text.empty() ? "The script could not be checked." : text);
}

} // namespace Forge
