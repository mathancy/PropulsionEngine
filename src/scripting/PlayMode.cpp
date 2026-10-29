// Standard headers first: some engine headers pull in <windows.h>, whose min/max macros break them.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "scripting/PlayMode.h"
#include "scripting/MouseLook.h"

#include "Engine.h"

#include "propulsion_ffi.h"

#include <GLFW/glfw3.h>

namespace Propulsion {

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
    /// The project's input: one InputMap per Input Mapping (same order as the setup's mappings), and which mapping each
    /// script entity is played with. An entity with no mapping gets no input.
    std::vector<InputMap> maps;
    std::map<int64_t, size_t> entityMap;
    /// What the player is doing when there is no window to read (the headless tests set this).
    InputState injected;
    /// `do SetPaused on Game paused:true`: while set, the scripts don't advance.
    bool paused = false;
    /// What `level name` says. The engine has one scene at a time, so it is the project's name.
    std::string levelName;
    /// Which scene object each script entity is (so a trace can say who asked, and name what it hit).
    const std::vector<Binding>* bindings = nullptr;
    /// Where scripts' traces are answered (the engine knows the shapes).
    PlayMode::TraceProvider traceProvider;

    static bool trace(void* user, int64_t me, const float* from, const float* to, float radius, PropulsionTraceHit* out) {
        auto* self = static_cast<EngineHost*>(user);
        if (!self->traceProvider || !self->bindings) return false;
        int fromScene = -1;
        for (const Binding& b : *self->bindings) {
            if (b.entity == me) fromScene = static_cast<int>(b.sceneIndex);
        }
        PlayMode::TraceResult r;
        if (!self->traceProvider(fromScene, from, to, radius, r)) return false;
        out->blocked = r.blocked;
        out->entity = -1;  // what it hit may have no script, and then there is nobody to name
        for (const Binding& b : *self->bindings) {
            if (r.sceneIndex >= 0 && b.sceneIndex == static_cast<size_t>(r.sceneIndex)) out->entity = b.entity;
        }
        for (int i = 0; i < 3; ++i) {
            out->location[i] = r.location[i];
            out->normal[i] = r.normal[i];
        }
        out->distance = r.distance;
        return true;
    }

    /// The InputMap an entity is played with, or null.
    InputMap* mapFor(int64_t entity) {
        auto it = entityMap.find(entity);
        return it == entityMap.end() || it->second >= maps.size() ? nullptr : &maps[it->second];
    }

    /// Play captures the mouse (hidden, unlimited movement) so it can turn the player. Whether it is captured
    /// right now also depends on the window having focus: alt-tabbing away gives the cursor back.
    bool captureMouse = false;
    MouseLook mouse;

    void setCursorMode(int mode) {
        if (!window || glfwGetInputMode(window, GLFW_CURSOR) == mode) return;
        glfwSetInputMode(window, GLFW_CURSOR, mode);
        // Raw motion skips the operating system's pointer acceleration, so the same hand movement always turns the same amount.
        if (mode == GLFW_CURSOR_DISABLED && glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        mouse.reset();
    }

    /// Hand the cursor back to the editor.
    void releaseMouse() {
        captureMouse = false;
        setCursorMode(GLFW_CURSOR_NORMAL);
    }

    /// Read the keyboard and mouse once per frame and give every Input Mapping what the player is doing, so "starts
    /// pressing" is true for exactly one step. `deltaTime` is the time the scripts are about to advance by, so the look
    /// axes scale to match it.
    void poll(bool allowInput, float deltaTime) {
        InputState now;
        if (window) {
            const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
            setCursorMode(captureMouse && focused ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            if (captureMouse && focused && allowInput) {
                double x = 0.0, y = 0.0;
                glfwGetCursorPos(window, &x, &y);
                mouse.update(x, y, deltaTime);
            } else {
                mouse.reset();
            }
            if (allowInput) {
                for (const InputKeys::KeyInfo& key : InputKeys::all()) {
                    if (key.kind == InputKeys::Kind::Key && glfwGetKey(window, key.code) == GLFW_PRESS) now.down.insert(key.name);
                    if (key.kind == InputKeys::Kind::MouseButton && glfwGetMouseButton(window, key.code) == GLFW_PRESS) now.down.insert(key.name);
                }
                now.mouseX = mouse.x;
                now.mouseY = mouse.y;
            }
        } else if (allowInput) {
            now = injected;
        }
        for (InputMap& map : maps) map.update(now);
    }

    // ---- callbacks the interpreter makes (see propulsion_ffi.h) ----

    // The entity asking is played with its own Input Mapping, so the same action can be a different key for each.
    static bool pressing(void* user, int64_t entity, const char* system, const char* input, int32_t kind) {
        auto* self = static_cast<EngineHost*>(user);
        if (std::string(system) != "Input") return kind == 1;
        const InputMap* map = self->mapFor(entity);
        switch (kind) {
            case 0: return map && map->pressing(input);
            case 1: return !map || !map->pressing(input);
            case 2: return map && map->started(input);
            default: return map && map->stopped(input);
        }
    }

    static float axis(void* user, int64_t entity, const char* system, const char* input) {
        auto* self = static_cast<EngineHost*>(user);
        if (std::string(system) != "Input") return 0.0f;
        const InputMap* map = self->mapFor(entity);
        return map ? map->axis(input) : 0.0f;
    }

    static void call(void* user, const char* system, const char* action, const char* args) {
        auto* self = static_cast<EngineHost*>(user);
        // Pausing is something the editor can really do: the scripts stop advancing.
        if (std::string(system) == "Game" && std::string(action) == "SetPaused") {
            self->paused = std::string(args).find("paused=true") != std::string::npos;
            self->log(LogCategory::Script, LogLevel::Info, std::string("the game is ") + (self->paused ? "paused" : "running again"));
            return;
        }
        self->log(LogCategory::Script, LogLevel::Info, std::string(system) + "." + action + (*args ? " " : "") + args +
                  "  (not connected to an engine system yet)");
    }

    static const char* levelNameOf(void* user) {
        return static_cast<EngineHost*>(user)->levelName.c_str();
    }

    static void print(void* user, const char* text) {
        static_cast<EngineHost*>(user)->log(LogCategory::Script, LogLevel::Info, text);
    }

};

/// A path the interpreter and the engine will agree on.
std::string absolutePath(const fs::path& projectDir, const std::string& relative) {
    return (projectDir / relative).lexically_normal().generic_string();
}

/// Send each line of `text` to the log.
void logLines(const PlayMode::Log& log, const char* category, LogLevel level, const std::string& text) {
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty()) log(category, level, line);
        start = end + 1;
    }
}

} // namespace

struct PlayMode::Impl {
    EngineHost host;
    Log log;
    PropulsionSession* session = nullptr;
    std::vector<Binding> bindings;
    InputSetup setup;
    size_t spawnedByHost = 0;
    bool reportedScriptSpawns = false;

    ~Impl() {
        host.releaseMouse();  // however Play ends, the cursor comes back
        if (session) propulsion_session_free(session);
    }
};

PlayMode::PlayMode(void* nativeWindow, Log log) : m_impl(std::make_unique<Impl>()) {
    m_impl->host.window = static_cast<GLFWwindow*>(nativeWindow);
    m_impl->host.bindings = &m_impl->bindings;
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

    PropulsionHost host{};
    host.user = &s.host;
    host.pressing = &EngineHost::pressing;
    host.axis = &EngineHost::axis;
    host.call = &EngineHost::call;
    host.print = &EngineHost::print;
    host.level_name = &EngineHost::levelNameOf;
    host.trace = &EngineHost::trace;
    s.host.levelName = fs::path(projectDir).filename().string();

    std::vector<const char*> cpaths;
    for (const std::string& p : paths) cpaths.push_back(p.c_str());
    char* errors = nullptr;
    s.session = propulsion_session_new(host, cpaths.data(), cpaths.size(), &errors);
    if (!s.session) {
        s.log(LogCategory::Compile, LogLevel::Error, "A script has a mistake, so nothing was started:");
        if (errors) logLines(s.log, LogCategory::Compile, LogLevel::Error, errors);
        propulsion_string_free(errors);
        return false;
    }

    for (size_t i = 0; i < scene.size(); ++i) {
        const SceneObject& source = scene[i];
        if (source.scriptFile.empty()) continue;
        std::string abs = absolutePath(projectDir, source.scriptFile);
        // The object is registered before its script starts: `when created` runs inside propulsion_spawn, and a trace made
        // there has to know which object is asking (so it can skip it). Entity ids are handed out in order.
        s.bindings.push_back({i, static_cast<int64_t>(propulsion_entity_count(s.session))});
        int64_t id = propulsion_spawn(s.session, abs.c_str(), &source.position.x, &source.rotation.x, &source.scale.x);
        if (id < 0) {
            s.bindings.pop_back();
            s.log(LogCategory::Script, LogLevel::Error, "Could not start " + source.scriptFile + " for " + source.name);
            continue;
        }
        s.bindings.back().entity = id;
        // Which Input Mapping this object is played with (an object with none gets no input).
        for (size_t k = 0; k < s.setup.mappings.size(); ++k) {
            if (!source.inputMappingId.empty() && s.setup.mappings[k].id == source.inputMappingId) s.host.entityMap[id] = k;
        }
        s.log(LogCategory::Script, LogLevel::Info, source.name + " is running " + source.scriptFile);
    }
    s.spawnedByHost = propulsion_entity_count(s.session);
    return true;
}

void PlayMode::setMouseCaptured(bool captured) {
    m_impl->host.captureMouse = captured;
    if (!captured) m_impl->host.releaseMouse();
}

bool PlayMode::mouseCaptured() const {
    return m_impl->host.window &&
           glfwGetInputMode(m_impl->host.window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
}

int PlayMode::runningCount() const {
    return static_cast<int>(m_impl->bindings.size());
}

float PlayMode::step(float deltaTime, std::vector<SceneObject>& scene, bool allowInput) {
    Impl& s = *m_impl;
    // A long hitch (a breakpoint, dragging the window) shouldn't throw an object across the level.
    const float stepTime = (std::min)(deltaTime, 0.1f);
    if (!s.session) return stepTime;  // nothing is scripted, but things can still fall

    s.host.poll(allowInput, stepTime);
    // (While the game is paused the scripts stand still, and the objects stay as they are.)
    if (!s.host.paused) propulsion_step(s.session, stepTime);

    if (char* warnings = propulsion_take_warnings(s.session)) {
        logLines(s.log, LogCategory::Script, LogLevel::Warning, warnings);
        propulsion_string_free(warnings);
    }

    for (const Binding& b : s.bindings) {
        if (b.sceneIndex >= scene.size()) continue;
        SceneObject& target = scene[b.sceneIndex];
        glm::vec3 loc, rot, scl;
        if (propulsion_get_transform(s.session, b.entity, &loc.x, &rot.x, &scl.x)) {
            target.position = loc;
            target.rotation = rot;
            target.scale = scl;
            // The renderer draws from the quaternion; the editor keeps it in step with the Euler angles.
            target.quaternion = glm::quat(glm::radians(rot));
            // `make Visibility.Shown false` hides the object, and `make Collision.Enabled false` lets things pass through it.
            bool shown = true, collides = true;
            if (propulsion_get_flags(s.session, b.entity, &shown, &collides)) {
                target.visible = shown;
                target.collisionEnabled = collides;
            }
        } else {
            target.visible = false;  // `destroy Self`
            target.collisionEnabled = false;
        }
    }

    if (!s.reportedScriptSpawns && propulsion_entity_count(s.session) > s.spawnedByHost) {
        s.reportedScriptSpawns = true;
        s.log(LogCategory::Script, LogLevel::Warning, "A script spawned new objects. They run, but don't appear in the viewport yet.");
    }
    return s.host.paused ? 0.0f : stepTime;
}

void PlayMode::setTraceProvider(TraceProvider provider) {
    m_impl->host.traceProvider = std::move(provider);
}

void PlayMode::reportTouch(size_t sceneIndexA, size_t sceneIndexB, bool touching) {
    Impl& s = *m_impl;
    if (!s.session) return;
    int64_t a = -1, b = -1;
    for (const Binding& binding : s.bindings) {
        if (binding.sceneIndex == sceneIndexA) a = binding.entity;
        if (binding.sceneIndex == sceneIndexB) b = binding.entity;
    }
    if (a >= 0 && b >= 0) propulsion_touch(s.session, a, b, touching);
}

void PlayMode::reportHit(size_t sceneIndexA, size_t sceneIndexB) {
    Impl& s = *m_impl;
    if (!s.session) return;
    int64_t a = -1, b = -1;
    for (const Binding& binding : s.bindings) {
        if (binding.sceneIndex == sceneIndexA) a = binding.entity;
        if (binding.sceneIndex == sceneIndexB) b = binding.entity;
    }
    if (a >= 0 && b >= 0) propulsion_hit(s.session, a, b);
}

void PlayMode::setInput(InputSetup setup) {
    Impl& s = *m_impl;
    s.setup = std::move(setup);
    s.host.maps.assign(s.setup.mappings.size(), InputMap());
    for (size_t k = 0; k < s.setup.mappings.size(); ++k) s.host.maps[k].setup(s.setup.actions, s.setup.mappings[k]);
}

void PlayMode::setScriptInputs(const std::vector<InputActionDef>& actions) {
    std::string buttons, axes;
    for (const InputActionDef& a : actions) (a.type == InputValueType::Axis ? axes : buttons) += a.name + "\n";
    propulsion_set_input_actions(buttons.c_str(), axes.c_str());
}

bool PlayMode::actionStartedThisStep(size_t sceneIndex, const std::string& action) const {
    const Impl& s = *m_impl;
    for (const Binding& b : s.bindings) {
        if (b.sceneIndex != sceneIndex) continue;
        auto it = s.host.entityMap.find(b.entity);
        return it != s.host.entityMap.end() && it->second < s.host.maps.size() && s.host.maps[it->second].started(action);
    }
    return false;
}

InputState& PlayMode::injectedInput() {
    return m_impl->host.injected;
}

void PlayMode::setPosition(size_t sceneIndex, float x, float y, float z) {
    Impl& s = *m_impl;
    if (!s.session) return;
    for (const Binding& b : s.bindings) {
        if (b.sceneIndex != sceneIndex) continue;
        glm::vec3 loc, rot, scl;
        if (!propulsion_get_transform(s.session, b.entity, &loc.x, &rot.x, &scl.x)) return;
        const float placed[3] = {x, y, z};
        propulsion_set_transform(s.session, b.entity, placed, &rot.x, &scl.x);
        return;
    }
}

std::vector<std::string> PlayMode::scriptsIn(const std::string& projectDir) {
    std::vector<std::string> found;
    std::error_code ec;
    fs::path root = fs::path(projectDir) / "Scripts";
    if (projectDir.empty() || !fs::is_directory(root, ec)) return found;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file() && it->path().extension() == ".prop") {
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
    fs::path file = dir / (safe + ".prop");
    for (int n = 2; fs::exists(file, ec); ++n) file = dir / (safe + "_" + std::to_string(n) + ".prop");

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
    bool fine = propulsion_check_file(absolutePath.c_str(), &errors);
    std::string text = errors ? errors : "";
    propulsion_string_free(errors);
    return fine ? "" : (text.empty() ? "The script could not be checked." : text);
}

} // namespace Propulsion
