#pragma once

// Shared pieces of the differential tests: a stub host that records what scripts ask for,
// and small helpers. Each test file includes the generated classes it needs, then this header's
// helpers drive them.

#include "ForgeScriptRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace harness {

using Forge::Object;
using namespace Forge::Script;

inline int failures = 0;
inline int checks = 0;

#define CHECK(cond, what)                                                          \
    do {                                                                           \
        ++harness::checks;                                                         \
        if (!(cond)) {                                                             \
            ++harness::failures;                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, what);                     \
        }                                                                          \
    } while (0)

inline bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

inline std::string Literal(const Value& v) {
    if (auto* s = std::get_if<std::string>(&v)) return "\"" + *s + "\"";
    if (auto* i = std::get_if<int>(&v)) return ToText(*i);
    if (auto* f = std::get_if<float>(&v)) return ToText(*f);
    if (auto* b = std::get_if<bool>(&v)) return ToText(*b);
    if (auto* v3 = std::get_if<glm::vec3>(&v)) return ToText(*v3);
    if (std::get_if<Object*>(&v)) return "entity";
    return "?";
}

// A host with no engine behind it. Its call log uses the same format as the interpreter's
// StubHost, so the two can be compared line for line.
struct StubHost : Host {
    std::set<std::string> held, started, stopped;
    std::map<std::string, float> axes;
    std::vector<std::string> calls, printed;
    std::string level;  // what `level name` says

    void Press(const std::string& input) {
        if (held.insert(input).second) started.insert(input);
    }
    void Release(const std::string& input) {
        if (held.erase(input)) stopped.insert(input);
    }
    bool Pressing(const std::string&, const std::string& i) override { return held.count(i) > 0; }
    bool Started(const std::string&, const std::string& i) override { return started.count(i) > 0; }
    bool Stopped(const std::string&, const std::string& i) override { return stopped.count(i) > 0; }
    float Axis(const std::string&, const std::string& i) override {
        auto it = axes.find(i);
        return it == axes.end() ? 0.0f : it->second;
    }
    void Call(const std::string& system, const std::string& action, const std::vector<Arg>& args) override {
        std::string line = system + "." + action;
        for (const Arg& a : args) line += " " + a.name + "=" + Literal(a.value);
        calls.push_back(line);
    }
    void Print(const std::string& text) override { printed.push_back(text); }
    std::string LevelName() override { return level; }
    void EndFrame() override {
        started.clear();
        stopped.clear();
    }
    int CountCalls(const std::string& call) const {
        return static_cast<int>(std::count(calls.begin(), calls.end(), call));
    }
    bool HasCall(const std::string& call) const { return CountCalls(call) > 0; }
};

inline void Run(World& w, double seconds, float dt) {
    const int steps = static_cast<int>(std::lround(seconds / dt));
    for (int i = 0; i < steps; ++i) w.Step(dt);
}

inline glm::vec3& Pos(Object* o) { return o->getTransform()->position; }

inline int Finish() {
    std::printf("%d checks, %d failed\n", checks, failures);
    if (failures == 0) std::printf("ALL PASSED\n");
    return failures == 0 ? 0 : 1;
}

}  // namespace harness
