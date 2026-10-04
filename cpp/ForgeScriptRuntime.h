#pragma once

/**
 * ForgeScriptRuntime.h
 * The small runtime that code generated from ForgeScript (.forge) stands on.
 *
 * Generated classes (FG_Player.generated.h, ...) derive from ScriptBehavior and are attached to
 * an engine Object through a ScriptComponent. A World delivers the frame, overlaps, events, and
 * delays to them, and a Host (implemented by the engine) supplies input, audio, UI, and logging.
 *
 * It is header-only and depends only on the engine's own Object/Component headers and glm.
 * Every behavior here mirrors the reference interpreter (SPEC 4.5a and 4.6), and the
 * differential test in cpp/tests checks that generated code and the interpreter agree.
 */

#include "core/Object.h"
#include "core/Component.h"

#include <glm/glm.hpp>

#include <algorithm>  // note: <windows.h> defines min/max macros, so the runtime writes (std::max)(a, b)
#include <charconv>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Forge::Script {

class World;
class ScriptBehavior;

// ─────────────────────────────────────────────────────────────────────────────
// Values passed to the host
// ─────────────────────────────────────────────────────────────────────────────

using Value = std::variant<int, float, bool, std::string, glm::vec2, glm::vec3, glm::vec4, Object*>;

struct Arg {
    std::string name;
    Value value;
};

// ─────────────────────────────────────────────────────────────────────────────
// Host: what the engine provides to scripts
// ─────────────────────────────────────────────────────────────────────────────

class Host {
public:
    virtual ~Host() = default;

    /// `Input is pressing jump`: held down this frame.
    virtual bool Pressing(const std::string& system, const std::string& input) = 0;
    /// `Input starts pressing jump`: went down this frame.
    virtual bool Started(const std::string& system, const std::string& input) = 0;
    /// `Input stops pressing jump`: came up this frame.
    virtual bool Stopped(const std::string& system, const std::string& input) = 0;
    /// `axis moveForward of Input`: an analog input between -1 and 1.
    virtual float Axis(const std::string& system, const std::string& input) { (void)system; (void)input; return 0.0f; }
    /// `do Play on Audio sound:"jump.wav"` calls Call("Audio", "Play", {{"sound", "jump.wav"}}).
    virtual void Call(const std::string& system, const std::string& action, const std::vector<Arg>& args) = 0;
    /// `print "..."`.
    virtual void Print(const std::string& text) = 0;
    /// `level name`: the name of the level being played.
    virtual std::string LevelName() { return {}; }
    /// `make Visibility.Shown false` and `make Collision.Enabled false` tell the engine.
    virtual void SetVisible(Object* object, bool visible) { (void)object; (void)visible; }
    virtual void SetCollision(Object* object, bool enabled) { (void)object; (void)enabled; }
    /// Called once at the end of every World::Step, so edge-triggered input can reset.
    virtual void EndFrame() {}
};

// ─────────────────────────────────────────────────────────────────────────────
// Text conversion (the same output as the interpreter's `joined with` and `text of`)
// ─────────────────────────────────────────────────────────────────────────────

inline std::string ToText(const std::string& v) { return v; }
inline std::string ToText(const char* v) { return v; }
inline std::string ToText(bool v) { return v ? "true" : "false"; }
inline std::string ToText(int v) { return std::to_string(v); }
inline std::string ToText(float v) {
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), v);
    return std::string(buffer, result.ptr);
}
inline std::string ToText(const glm::vec2& v) { return "(" + ToText(v.x) + ", " + ToText(v.y) + ")"; }
inline std::string ToText(const glm::vec3& v) { return "(" + ToText(v.x) + ", " + ToText(v.y) + ", " + ToText(v.z) + ")"; }
inline std::string ToText(const glm::vec4& v) {
    return "color(" + ToText(v.x) + ", " + ToText(v.y) + ", " + ToText(v.z) + ", " + ToText(v.w) + ")";
}

// ─────────────────────────────────────────────────────────────────────────────
// Math the phrases use (`clamp`, `map`, `normalized`, ...). Each does exactly what the interpreter's does.
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr float kDegrees = 57.29577951308232f;

template <typename A, typename B, typename C>
auto Clamp(A value, B low, C high) {
    using R = std::common_type_t<A, B, C>;
    const R v = static_cast<R>(value), lo = static_cast<R>(low), hi = static_cast<R>(high);
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float MapRange(float value, float fromMin, float fromMax, float toMin, float toMax) {
    float t = fromMax == fromMin ? 0.0f : (value - fromMin) / (fromMax - fromMin);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return toMin + (toMax - toMin) * t;
}

inline float VectorLength(const glm::vec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline float VectorLength(const glm::vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline glm::vec2 Normalized(const glm::vec2& v) {
    const float len = VectorLength(v);
    return len == 0.0f ? glm::vec2(0.0f) : glm::vec2(v.x / len, v.y / len);
}
inline glm::vec3 Normalized(const glm::vec3& v) {
    const float len = VectorLength(v);
    return len == 0.0f ? glm::vec3(0.0f) : glm::vec3(v.x / len, v.y / len, v.z / len);
}
inline float DotProduct(const glm::vec2& a, const glm::vec2& b) { return a.x * b.x + a.y * b.y; }
inline float DotProduct(const glm::vec3& a, const glm::vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// `glm::quat(radians(rotation)) * v`: the engine's Euler rotation applied to a vector.
inline glm::vec3 RotateByEuler(const glm::vec3& rotation, const glm::vec3& v) { return glm::quat(glm::radians(rotation)) * v; }

template <typename T, typename U>
int IndexOf(const std::vector<T>& list, const U& item) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == item) return static_cast<int>(i);
    }
    return -1;
}

template <typename T, typename U>
bool Contains(const std::vector<T>& list, const U& item) { return IndexOf(list, item) >= 0; }

inline float MixValue(float a, float b, float t) { return a + (b - a) * t; }
inline glm::vec2 MixValue(const glm::vec2& a, const glm::vec2& b, float t) { return glm::vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
inline glm::vec3 MixValue(const glm::vec3& a, const glm::vec3& b, float t) {
    return glm::vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

/// Whether a type can travel in a `Value` (the arguments of `tell` and of system calls).
template <typename T>
inline constexpr bool kInValue = std::is_same_v<T, int> || std::is_same_v<T, float> || std::is_same_v<T, bool> ||
                                 std::is_same_v<T, std::string> || std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> ||
                                 std::is_same_v<T, glm::vec4> || std::is_same_v<T, Object*>;

/// Set a variable from an exposed override, turning a whole number into a decimal when needed.
inline bool AssignValue(float& dst, const Value& v) {
    if (auto* f = std::get_if<float>(&v)) { dst = *f; return true; }
    if (auto* i = std::get_if<int>(&v)) { dst = static_cast<float>(*i); return true; }
    return false;
}
template <typename T>
bool AssignValue(T& dst, const Value& v) {
    if (auto* p = std::get_if<T>(&v)) { dst = *p; return true; }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// What a trace found: the `hit` of a script (`remember Ahead trace from A to B`, then `Ahead.Entity`)
// ─────────────────────────────────────────────────────────────────────────────

struct HitResult {
    bool Blocked = false;
    Object* Entity = nullptr;
    glm::vec3 Location{};
    glm::vec3 Normal{};
    float Distance = 0.0f;
    bool operator==(const HitResult& o) const {
        return Blocked == o.Blocked && Entity == o.Entity && Location == o.Location && Normal == o.Normal && Distance == o.Distance;
    }
    bool operator!=(const HitResult& o) const { return !(*this == o); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Timer: `variable name:SinceHit type:timer`
// ─────────────────────────────────────────────────────────────────────────────

struct Timer {
    double since = 0.0;
    inline void Reset(const World& world);
    inline double Elapsed(const World& world) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// ScriptBehavior: the base class of every generated script
// ─────────────────────────────────────────────────────────────────────────────

class ScriptBehavior {
public:
    virtual ~ScriptBehavior() = default;

    /// The script's name, like "Player". Used to match `Self touches Player`.
    virtual const char* ScriptName() const = 0;
    /// `tag name:"player"`.
    virtual std::vector<std::string> Tags() const { return {}; }
    virtual std::unique_ptr<ScriptBehavior> Clone() const = 0;
    /// Set an `exposed:true` variable on a placed instance. Returns false if it isn't exposed.
    virtual bool SetExposed(const std::string& name, const Value& value) { (void)name; (void)value; return false; }

    // Entry points, called by the World. Generated classes override the ones they use.
    virtual void OnBeginPlay() {}
    virtual void OnDestroy() {}
    virtual void OnTick(float deltaTime) { (void)deltaTime; }
    virtual void OnOverlapBegin(Object* other) { (void)other; }
    virtual void OnOverlapEnd(Object* other) { (void)other; }
    virtual void OnHit(Object* other) { (void)other; }
    /// `when damaged as Amount from Attacker:`
    virtual void OnDamaged(float amount, Object* source) { (void)amount; (void)source; }
    /// `tell Foe to TakeDamage amount:5`: run the action called `action` if this script has one.
    virtual bool Message(const std::string& action, const std::vector<Arg>& args) { (void)action; (void)args; return false; }
    virtual void OnOwnEvent(const std::string& name) { (void)name; }
    virtual void OnEventFrom(ScriptBehavior* sender, const std::string& name) { (void)sender; (void)name; }
    /// Run `when <Variable> changes:` handlers for anything that changed. Returns true if any ran.
    virtual bool FlushChanges() { return false; }

    // Set by the World.
    Object* self = nullptr;
    World* world = nullptr;
    double born = 0.0;

    inline bool IsAlive() const;

protected:
    // Helpers the generated code calls. They live here so they can warn through the World.
    inline bool Present(Object* o) const;
    inline bool IntervalDue(int index, double seconds);

    template <typename A, typename B>
    auto Div(A a, B b);
    template <typename A, typename B>
    auto Mod(A a, B b);
    template <typename T>
    T Item(const std::vector<T>& list, int index);

    template <typename T, typename U>
    static T Cap(T value, U limit) { return value > limit ? static_cast<T>(limit) : value; }
    template <typename T, typename U>
    static T Floor(T value, U limit) { return value < limit ? static_cast<T>(limit) : value; }

    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    static glm::vec2 Lerp(const glm::vec2& a, const glm::vec2& b, float t) { return a + (b - a) * t; }
    static glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; }

    template <typename T, typename U>
    static void Erase(std::vector<T>& list, const U& value) {
        auto it = std::find(list.begin(), list.end(), value);
        if (it != list.end()) list.erase(it);
    }

    template <typename T>
    void EraseAt(std::vector<T>& list, int index);

    template <typename T, typename U>
    static void AddUnique(std::vector<T>& list, const U& value) {
        if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
    }

    /// An argument of a `tell`, by name (a whole number given for a decimal is converted).
    template <typename T>
    static T Take(const std::vector<Arg>& args, const char* name) {
        if constexpr (kInValue<T>) {
            for (const Arg& a : args) {
                if (a.name != name) continue;
                if (auto* p = std::get_if<T>(&a.value)) return *p;
                if constexpr (std::is_same_v<T, float>) {
                    if (auto* i = std::get_if<int>(&a.value)) return static_cast<float>(*i);
                }
            }
        } else {
            (void)args; (void)name;
        }
        return T{};
    }

private:
    std::unordered_map<int, double> due_;
};

// ─────────────────────────────────────────────────────────────────────────────
// ScriptComponent: attaches a behavior to an engine Object
// ─────────────────────────────────────────────────────────────────────────────

class ScriptComponent : public Forge::Component {
public:
    explicit ScriptComponent(std::unique_ptr<ScriptBehavior> behavior) : behavior_(std::move(behavior)) {}

    Forge::ComponentType getType() const override { return Forge::ComponentType::Script; }
    std::unique_ptr<Forge::Component> clone() const override {
        return std::make_unique<ScriptComponent>(behavior_ ? behavior_->Clone() : nullptr);
    }
    ScriptBehavior* behavior() const { return behavior_.get(); }

private:
    std::unique_ptr<ScriptBehavior> behavior_;
};

// ─────────────────────────────────────────────────────────────────────────────
// ScriptRegistry: finds a script by the path a scene or object refers to it by
// ─────────────────────────────────────────────────────────────────────────────

/// One script the build knows about. `path` is the .forge file as the build was given it
/// (`behaviors/door.forge`); that path, not the class name, is what a scene should store.
struct ScriptEntry {
    const char* path;
    const char* name;  ///< The script's name, like "Door".
    /// Create an Object running this script at `at`, with exposed variables set from `overrides`.
    Object* (*spawn)(World& world, const glm::vec3& at, const std::vector<Arg>& overrides);
};

/// Every script in the build. Defined in the generated ForgeScriptRegistry.generated.cpp.
const std::vector<ScriptEntry>& ScriptRegistry();

inline const ScriptEntry* FindScript(const std::string& path) {
    for (const ScriptEntry& entry : ScriptRegistry()) {
        if (path == entry.path) return &entry;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// World: runs the scripts
// ─────────────────────────────────────────────────────────────────────────────

class World {
public:
    explicit World(Host& host) : host_(host) {}

    Host& host() { return host_; }
    double Time() const { return time; }

    double time = 0.0;  ///< Seconds since the world started.
    float dt = 0.0f;    ///< Length of the current frame (what `per second` multiplies by).
    std::vector<std::string> warnings;  ///< Things scripts did that were skipped (SPEC 4.6).

    void Warn(const std::string& message) { warnings.push_back(message); }

    // ── Creating and removing objects ─────────────────────────────────────────

    /// Create an Object running script T. Its OnBeginPlay runs before this returns.
    template <typename T>
    Object* Spawn(const glm::vec3& at, const std::vector<Arg>& overrides = {}) {
        Object* o = SpawnFromScript<T>(at, overrides);
        Pump();
        return o;
    }

    /// Like Spawn, without delivering events afterwards. Generated code uses this while a handler runs.
    template <typename T>
    Object* SpawnFromScript(const glm::vec3& at, const std::vector<Arg>& overrides = {}) {
        auto object = std::make_unique<Object>(T::kScriptName);
        Object* o = object.get();
        objects_.push_back(std::move(object));
        o->getTransform()->position = at;

        auto behavior = std::make_unique<T>();
        T* b = behavior.get();
        b->self = o;
        b->world = this;
        b->born = time;
        o->addComponent<ScriptComponent>(std::move(behavior));
        for (const Arg& arg : overrides) {
            if (!b->SetExposed(arg.name, arg.value)) {
                Warn(std::string("`") + arg.name + "` isn't exposed, so it can't be set on a placed instance");
            }
        }
        b->OnBeginPlay();
        return o;
    }

    /// The one live Object running script T, created if there isn't one yet.
    /// (`do ShowCoins on Hud` goes to this object.)
    template <typename T>
    T* Instance() {
        for (auto& o : objects_) {
            if (o->isActive()) {
                if (T* t = dynamic_cast<T*>(ScriptOf(o.get()))) return t;
            }
        }
        Object* o = SpawnFromScript<T>(glm::vec3(0.0f));
        return As<T>(o);
    }

    void Destroy(Object* o) {
        if (!Present(o)) {
            Warn("tried to destroy an entity that is missing; skipped");
            return;
        }
        if (ScriptBehavior* b = ScriptOf(o)) b->OnDestroy();
        o->setActive(false);
        // What was attached to it stays where it is.
        for (auto it = follows_.begin(); it != follows_.end();) {
            if (it->second.parent == o) it = follows_.erase(it); else ++it;
        }
    }

    // ── Looking things up ────────────────────────────────────────────────────

    bool Present(Object* o) const { return o != nullptr && o->isActive(); }

    ScriptBehavior* ScriptOf(Object* o) const {
        if (!o) return nullptr;
        for (const auto& c : o->getAllComponents()) {
            if (c->getType() == Forge::ComponentType::Script) {
                return static_cast<ScriptComponent*>(c.get())->behavior();
            }
        }
        return nullptr;
    }

    template <typename T>
    T* As(Object* o) const {
        return dynamic_cast<T*>(ScriptOf(o));
    }

    Object* FindByTag(const std::string& tag) const {
        for (const auto& o : objects_) {
            if (o->isActive() && HasTag(o.get(), tag)) return o.get();
        }
        return nullptr;
    }

    std::vector<Object*> AllByTag(const std::string& tag) const {
        std::vector<Object*> found;
        for (const auto& o : objects_) {
            if (o->isActive() && HasTag(o.get(), tag)) found.push_back(o.get());
        }
        return found;
    }

    const std::vector<std::unique_ptr<Object>>& Objects() const { return objects_; }

    // ── Time ─────────────────────────────────────────────────────────────────

    /// Advance by `deltaTime` seconds: delayed blocks that are due, then every object's OnTick,
    /// delivering events and variable changes after each.
    void Step(float deltaTime) {
        dt = deltaTime;
        time += deltaTime;
        RunDuePending();
        RunTimelines();
        const size_t count = objects_.size();  // objects spawned this frame start next frame
        for (size_t i = 0; i < count; ++i) {
            Object* o = objects_[i].get();
            if (!o->isActive()) continue;
            if (ScriptBehavior* b = ScriptOf(o)) b->OnTick(deltaTime);
            Pump();
        }
        FollowParents();
        host_.EndFrame();
    }

    /// `after 2 seconds:`. The block is dropped if its object is gone by then.
    void After(ScriptBehavior* owner, float seconds, std::function<void()> block) {
        pending_.push_back({time + (std::max)(0.0f, seconds), nextSeq_++, owner, std::move(block)});
    }

    /// `send Died`: delivered after the current handler finishes.
    void Send(ScriptBehavior* sender, const std::string& name) { events_.push_back({sender, name}); }

    /// `animate Lift from 0.0 to 5.0 over 2 seconds:` runs `body` every frame for that long, the value moving
    /// from `from` to `to`. The last run is with the value at `to`.
    template <typename A, typename B, typename F>
    void Animate(ScriptBehavior* owner, A from, B to, float seconds, F body) {
        using T = std::common_type_t<A, B>;
        const T a = static_cast<T>(from);
        const T b = static_cast<T>(to);
        timelines_.push_back({nextSeq_++, owner, time, (std::max)(0.0f, seconds), [a, b, body](float progress) mutable { body(MixValue(a, b, progress)); }});
    }

    // ── Overlaps and hits (reported by the engine) ───────────────────────────

    void Touch(Object* a, Object* b) { OverlapChange(a, b, Overlap::Begin); }
    void Untouch(Object* a, Object* b) { OverlapChange(a, b, Overlap::End); }
    void Hit(Object* a, Object* b) { OverlapChange(a, b, Overlap::Hit); }

    /// `damage Foe by 5.0`: runs the target's `when damaged` handlers right away.
    void Damage(Object* target, float amount, Object* source) {
        if (!Present(target)) {
            Warn("tried to damage an entity that is missing; skipped");
            return;
        }
        if (ScriptBehavior* b = ScriptOf(target)) b->OnDamaged(amount, source ? source : target);
    }

    /// `tell Foe to TakeDamage amount:5`: skipped quietly when the entity has no such action.
    void Tell(Object* target, const std::string& action, const std::vector<Arg>& args) {
        if (!Present(target)) return;
        if (ScriptBehavior* b = ScriptOf(target)) b->Message(action, args);
    }

    /// `attach Hat to Player`: the hat keeps where it is now, measured from the player, and follows from here on.
    void Attach(Object* child, Object* parent) {
        if (!Present(child) || !Present(parent)) {
            Warn("tried to attach an entity that is missing; skipped");
            return;
        }
        if (child == parent) {
            Warn("`attach` was skipped: an entity can't be attached to itself");
            return;
        }
        int hops = 0;
        for (Object* p = parent; p != nullptr && hops < 64; ++hops) {
            if (p == child) {
                Warn("`attach` was skipped: that would make two entities follow each other");
                return;
            }
            auto it = follows_.find(p);
            p = it == follows_.end() ? nullptr : it->second.parent;
        }
        const Forge::TransformComponent* c = child->getTransform();
        const Forge::TransformComponent* p = parent->getTransform();
        const glm::quat inverse = glm::conjugate(glm::quat(glm::radians(p->rotation)));
        follows_[child] = Follow{parent, inverse * (c->position - p->position), c->rotation - p->rotation};
    }

    void Detach(Object* child) {
        if (!Present(child)) {
            Warn("tried to detach an entity that is missing; skipped");
            return;
        }
        follows_.erase(child);
    }

    // ── Visibility and collision (`make Visibility.Shown false`) ─────────────

    bool IsShown(Object* o) const { auto it = shown_.find(o); return it == shown_.end() ? Present(o) : it->second; }
    bool IsCollidable(Object* o) const { auto it = collides_.find(o); return it == collides_.end() ? Present(o) : it->second; }
    void SetShown(Object* o, bool shown) {
        if (!Present(o)) { Warn("a missing entity's field was skipped"); return; }
        shown_[o] = shown;
        host_.SetVisible(o, shown);
    }
    void SetCollision(Object* o, bool enabled) {
        if (!Present(o)) { Warn("a missing entity's field was skipped"); return; }
        collides_[o] = enabled;
        host_.SetCollision(o, enabled);
    }

    // ── Random numbers (`random integer from 1 to 6`) ────────────────────────

    /// A world starts from the same seed every time, so a test sees the same rolls; a game seeds from the clock.
    void Seed(uint64_t seed) { randomState_ = seed == 0 ? 1 : seed; }
    uint64_t NextRandom() {
        uint64_t x = randomState_;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        randomState_ = x;
        return x * 0x2545F4914F6CDD1DULL;
    }
    float RandomDecimal(float low, float high) {
        const float unit = static_cast<float>(NextRandom() >> 40) / 16777216.0f;
        return low + (high - low) * unit;
    }
    int RandomInteger(int low, int high) {
        if (high < low) return low;
        const uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(high) - low + 1);
        return static_cast<int>(low + static_cast<int64_t>(NextRandom() % span));
    }

    // ── Traces (`trace from A to B`, `sweep from A to B radius R`) ───────────

    /// What a line from `from` to `to` (this thick) hits first. An object counts as a ball half as wide as its
    /// largest scale until the engine reports real shapes; `me` is skipped.
    HitResult Trace(Object* me, const glm::vec3& from, const glm::vec3& to, float radius) {
        const glm::vec3 d(to.x - from.x, to.y - from.y, to.z - from.z);
        const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        Object* best = nullptr;
        float bestT = 0.0f;
        if (length > 0.0f) {
            const glm::vec3 dir(d.x / length, d.y / length, d.z / length);
            for (const auto& owned : objects_) {
                Object* o = owned.get();
                if (!o->isActive() || !IsCollidable(o) || o == me) continue;
                const glm::vec3 c = o->getTransform()->position;
                const glm::vec3 sc = o->getTransform()->scale;
                const float r = (std::max)((std::max)(sc.x, sc.y), sc.z) * 0.5f + radius;
                const glm::vec3 oc(from.x - c.x, from.y - c.y, from.z - c.z);
                const float b = oc.x * dir.x + oc.y * dir.y + oc.z * dir.z;
                const float cc = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - r * r;
                if (cc > 0.0f && b > 0.0f) continue;  // outside the ball and looking away from it
                const float disc = b * b - cc;
                if (disc < 0.0f) continue;
                float t = -b - std::sqrt(disc);
                if (t < 0.0f) t = 0.0f;
                if (t > length) continue;
                if (!best || t < bestT) {
                    best = o;
                    bestT = t;
                }
            }
        }
        HitResult h;
        if (best) {
            const glm::vec3 point(from.x + d.x / length * bestT, from.y + d.y / length * bestT, from.z + d.z / length * bestT);
            const glm::vec3 c = best->getTransform()->position;
            const glm::vec3 n(point.x - c.x, point.y - c.y, point.z - c.z);
            const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            h.Blocked = true;
            h.Entity = best;
            h.Location = point;
            h.Normal = nl == 0.0f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(n.x / nl, n.y / nl, n.z / nl);
            h.Distance = bestT;
        } else {
            h.Location = to;
            h.Distance = length;
        }
        return h;
    }
    HitResult Trace(Object* me, Object* from, Object* to, float radius) {
        if (!Present(from) || !Present(to)) Warn("a position was missing, so it counted as (0, 0, 0)");
        return Trace(me, Present(from) ? from->getTransform()->position : glm::vec3(0.0f), Present(to) ? to->getTransform()->position : glm::vec3(0.0f), radius);
    }

    // ── Directions, rotations and easing ─────────────────────────────────────

    /// `forward of Player`: an axis turned by where the entity faces.
    glm::vec3 Direction(Object* o, const glm::vec3& axis) {
        if (!Present(o)) {
            Warn("an entity was missing, so its direction counted as unturned");
            return axis;
        }
        return RotateByEuler(o->getTransform()->rotation, axis);
    }

    /// `rotation looking from A to B`: the pitch and yaw that make forward (-Z) point from A at B.
    glm::vec3 LookAt(const glm::vec3& from, const glm::vec3& to) const {
        const glm::vec3 d(to.x - from.x, to.y - from.y, to.z - from.z);
        const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len == 0.0f) return glm::vec3(0.0f);
        float up = d.y / len;
        up = up > 1.0f ? 1.0f : (up < -1.0f ? -1.0f : up);
        const float pitch = std::asin(up) * kDegrees;
        const float yaw = std::atan2(-d.x, -d.z) * kDegrees;
        return glm::vec3(pitch + 0.0f, yaw + 0.0f, 0.0f);
    }
    glm::vec3 LookAt(Object* from, Object* to) {
        if (!Present(from) || !Present(to)) Warn("a position was missing, so it counted as (0, 0, 0)");
        return LookAt(Present(from) ? from->getTransform()->position : glm::vec3(0.0f), Present(to) ? to->getTransform()->position : glm::vec3(0.0f));
    }

    float Ease(float current, float target, float speed) const {
        if (speed <= 0.0f) return target;
        const float d = target - current;
        if (d * d < 1e-8f) return target;
        return current + d * StepFraction(speed);
    }
    glm::vec2 Ease(const glm::vec2& current, const glm::vec2& target, float speed) const {
        if (speed <= 0.0f) return target;
        const glm::vec2 d(target.x - current.x, target.y - current.y);
        if (d.x * d.x + d.y * d.y < 1e-8f) return target;
        const float f = StepFraction(speed);
        return glm::vec2(current.x + d.x * f, current.y + d.y * f);
    }
    glm::vec3 Ease(const glm::vec3& current, const glm::vec3& target, float speed) const {
        if (speed <= 0.0f) return target;
        const glm::vec3 d(target.x - current.x, target.y - current.y, target.z - current.z);
        if (d.x * d.x + d.y * d.y + d.z * d.z < 1e-8f) return target;
        const float f = StepFraction(speed);
        return glm::vec3(current.x + d.x * f, current.y + d.y * f, current.z + d.z * f);
    }
    /// `turn Facing toward Goal at 90.0`: like Ease, but the short way round (angles in degrees).
    glm::vec3 Turn(const glm::vec3& current, const glm::vec3& target, float speed) const {
        if (speed <= 0.0f) return target;
        const glm::vec3 d(HalfTurn(target.x - current.x), HalfTurn(target.y - current.y), HalfTurn(target.z - current.z));
        if (d.x * d.x + d.y * d.y + d.z * d.z < 1e-8f) return target;
        const float f = StepFraction(speed);
        return glm::vec3(current.x + d.x * f, current.y + d.y * f, current.z + d.z * f);
    }

    /// `cast Foe to Goblin`: the entity if it runs that script, otherwise a missing entity.
    template <typename T>
    Object* CastTo(Object* o) const { return Present(o) && As<T>(o) ? o : nullptr; }

    /// Deliver queued events and run `when <Variable> changes:` handlers until nothing more happens.
    void Pump() {
        int rounds = 0;
        for (;;) {
            DeliverEvents();
            const bool changed = RunChangeHandlers();
            if (!changed && events_.empty()) break;
            if (++rounds > 64) {
                Warn("variables and events kept triggering each other; the rest were dropped");
                events_.clear();
                break;
            }
        }
    }

    // ── Helpers for generated code ───────────────────────────────────────────

    Forge::TransformComponent& TransformOf(Object* o) {
        if (Present(o)) return *o->getTransform();
        Warn("a missing entity's transform was read as zero");
        missingTransform_ = Forge::TransformComponent();
        return missingTransform_;
    }

    float Distance(const glm::vec3& a, const glm::vec3& b) const { return glm::length(a - b); }
    float Distance(const glm::vec2& a, const glm::vec2& b) const { return glm::length(a - b); }
    float Distance(Object* a, Object* b) {
        if (!Present(a) || !Present(b)) {
            Warn("a position was missing, so it counted as (0, 0, 0)");
        }
        const glm::vec3 pa = Present(a) ? a->getTransform()->position : glm::vec3(0.0f);
        const glm::vec3 pb = Present(b) ? b->getTransform()->position : glm::vec3(0.0f);
        return glm::length(pa - pb);
    }

    /// `move Self forward by 5`: relative to where the entity faces (-Z is forward).
    void MoveRelative(Object* o, const glm::vec3& direction, float amount) {
        if (!Present(o)) {
            Warn("tried to move an entity that is missing; skipped");
            return;
        }
        o->getTransform()->translateLocal(direction * amount);
    }

    /// `move Transform.Loc toward Target by 1`.
    static void MoveToward(glm::vec3& position, const glm::vec3& target, float step) {
        const glm::vec3 delta = target - position;
        const float distance = glm::length(delta);
        position = (distance <= step || distance == 0.0f) ? target : position + delta * (step / distance);
    }

private:
    friend class ScriptBehavior;

    struct Pending {
        double due;
        uint64_t seq;
        ScriptBehavior* owner;
        std::function<void()> block;
    };
    struct QueuedEvent {
        ScriptBehavior* sender;
        std::string name;
    };
    struct Timeline {
        uint64_t seq;
        ScriptBehavior* owner;
        double start;
        double duration;
        std::function<void(float)> step;
    };
    /// `attach`: who an entity follows, and where it sits relative to them.
    struct Follow {
        Object* parent;
        glm::vec3 localLoc;
        glm::vec3 localRot;
    };

    bool HasTag(Object* o, const std::string& tag) const {
        ScriptBehavior* b = ScriptOf(o);
        if (!b) return false;
        for (const std::string& t : b->Tags()) {
            if (t == tag) return true;
        }
        return false;
    }

    void RunDuePending() {
        std::vector<Pending> due;
        for (size_t i = 0; i < pending_.size();) {
            if (pending_[i].due <= time) {
                due.push_back(std::move(pending_[i]));
                pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        std::stable_sort(due.begin(), due.end(), [](const Pending& x, const Pending& y) {
            return x.due != y.due ? x.due < y.due : x.seq < y.seq;
        });
        for (Pending& p : due) {
            if (p.owner && p.owner->IsAlive()) {
                p.block();
                Pump();
            }
        }
    }

    enum class Overlap { Begin, End, Hit };

    void OverlapChange(Object* a, Object* b, Overlap kind) {
        for (auto [x, y] : {std::pair<Object*, Object*>{a, b}, std::pair<Object*, Object*>{b, a}}) {
            if (!Present(x) || !y) continue;
            if (ScriptBehavior* bx = ScriptOf(x)) {
                switch (kind) {
                    case Overlap::Begin: bx->OnOverlapBegin(y); break;
                    case Overlap::End: bx->OnOverlapEnd(y); break;
                    case Overlap::Hit: bx->OnHit(y); break;
                }
            }
        }
        Pump();
    }

    float StepFraction(float speed) const {
        const float f = dt * speed;
        return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    }

    static float HalfTurn(float d) {
        d = std::fmod(d, 360.0f);
        if (d > 180.0f) d -= 360.0f; else if (d < -180.0f) d += 360.0f;
        return d;
    }

    /// Run every `animate` block one more frame; one that is finished (or whose owner is gone) is dropped.
    void RunTimelines() {
        const size_t count = timelines_.size();  // blocks started this frame run from the next one
        std::vector<uint64_t> finished;
        for (size_t i = 0; i < count; ++i) {
            auto step = timelines_[i].step;
            ScriptBehavior* owner = timelines_[i].owner;
            const uint64_t seq = timelines_[i].seq;
            const double start = timelines_[i].start;
            const double duration = timelines_[i].duration;
            if (!owner || !owner->IsAlive()) {
                finished.push_back(seq);
                continue;
            }
            double progress = duration <= 0.0 ? 1.0 : (time - start) / duration;
            progress = progress < 0.0 ? 0.0 : (progress > 1.0 ? 1.0 : progress);
            step(static_cast<float>(progress));
            Pump();
            if (progress >= 1.0) finished.push_back(seq);
        }
        timelines_.erase(std::remove_if(timelines_.begin(), timelines_.end(),
                                        [&](const Timeline& t) { return std::find(finished.begin(), finished.end(), t.seq) != finished.end(); }),
                         timelines_.end());
    }

    /// Put every attached entity where its parent says it is.
    void FollowParents() {
        for (const auto& o : objects_) FollowOne(o.get(), 0);
    }

    void FollowOne(Object* o, int depth) {
        if (depth > 64 || !Present(o)) return;
        auto it = follows_.find(o);
        if (it == follows_.end()) return;
        const Follow f = it->second;
        if (!Present(f.parent)) {
            follows_.erase(o);
            return;
        }
        FollowOne(f.parent, depth + 1);
        Forge::TransformComponent* t = o->getTransform();
        const Forge::TransformComponent* p = f.parent->getTransform();
        t->position = p->position + RotateByEuler(p->rotation, f.localLoc);
        t->rotation = p->rotation + f.localRot;
        t->updateQuaternion();
    }

    void DeliverEvents() {
        int delivered = 0;
        while (!events_.empty()) {
            QueuedEvent e = std::move(events_.front());
            events_.pop_front();
            if (++delivered > 10000) {
                Warn("events kept sending each other; the rest were dropped");
                events_.clear();
                break;
            }
            if (e.sender && e.sender->IsAlive()) e.sender->OnOwnEvent(e.name);
            for (size_t i = 0; i < objects_.size(); ++i) {
                Object* o = objects_[i].get();
                if (!o->isActive()) continue;
                if (ScriptBehavior* b = ScriptOf(o)) b->OnEventFrom(e.sender, e.name);
            }
        }
    }

    bool RunChangeHandlers() {
        bool any = false;
        for (size_t i = 0; i < objects_.size(); ++i) {
            Object* o = objects_[i].get();
            if (!o->isActive()) continue;
            if (ScriptBehavior* b = ScriptOf(o)) any = b->FlushChanges() || any;
        }
        return any;
    }

    Host& host_;
    std::vector<std::unique_ptr<Object>> objects_;
    std::vector<Pending> pending_;
    std::deque<QueuedEvent> events_;
    std::vector<Timeline> timelines_;
    std::unordered_map<Object*, Follow> follows_;
    std::unordered_map<Object*, bool> shown_;
    std::unordered_map<Object*, bool> collides_;
    uint64_t randomState_ = 0x2545F4914F6CDD1DULL;
    uint64_t nextSeq_ = 0;
    Forge::TransformComponent missingTransform_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Inline definitions that need both World and ScriptBehavior
// ─────────────────────────────────────────────────────────────────────────────

inline void Timer::Reset(const World& world) { since = world.Time(); }
inline double Timer::Elapsed(const World& world) const { return world.Time() - since; }

inline bool ScriptBehavior::IsAlive() const { return self != nullptr && self->isActive(); }
inline bool ScriptBehavior::Present(Object* o) const { return o != nullptr && o->isActive(); }

inline bool ScriptBehavior::IntervalDue(int index, double seconds) {
    auto it = due_.find(index);
    if (it == due_.end()) it = due_.emplace(index, born + seconds).first;
    if (world->time >= it->second) {
        double next = it->second + seconds;
        while (next <= world->time) next += seconds;
        it->second = next;
        return true;
    }
    return false;
}

template <typename A, typename B>
auto ScriptBehavior::Div(A a, B b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if (b == 0) { world->Warn("divided by zero; the result is 0"); return 0; }
        return a / b;
    } else if constexpr (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>) {
        if (b == 0) { world->Warn("divided by zero; the result is 0"); return 0.0f; }
        return static_cast<float>(a) / static_cast<float>(b);
    } else {
        // A vector divided by a number.
        if (b == 0) { world->Warn("divided by zero; the result is 0"); return A(0.0f); }
        return a / static_cast<float>(b);
    }
}

template <typename A, typename B>
auto ScriptBehavior::Mod(A a, B b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if (b == 0) { world->Warn("divided by zero; the result is 0"); return 0; }
        return a % b;
    } else {
        if (b == 0) { world->Warn("divided by zero; the result is 0"); return 0.0f; }
        return std::fmod(static_cast<float>(a), static_cast<float>(b));
    }
}

template <typename T>
void ScriptBehavior::EraseAt(std::vector<T>& list, int index) {
    if (index >= 0 && static_cast<size_t>(index) < list.size()) {
        list.erase(list.begin() + index);
    } else {
        world->Warn("item " + std::to_string(index) + " is past the end of a list of " + std::to_string(list.size()) + "; nothing was removed");
    }
}

template <typename T>
T ScriptBehavior::Item(const std::vector<T>& list, int index) {
    if (index >= 0 && static_cast<size_t>(index) < list.size()) return list[static_cast<size_t>(index)];
    world->Warn("item " + std::to_string(index) + " is past the end of a list of " + std::to_string(list.size()) + "; used the default");
    return T{};
}

}  // namespace Forge::Script
