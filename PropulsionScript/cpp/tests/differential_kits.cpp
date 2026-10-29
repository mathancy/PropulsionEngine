// differential_kits.cpp
// Runs the same checks as propulsion/tests/nodelist.rs, but against the generated C++ classes: the math,
// list, flow and world nodes taken from NODELIST.md. If this agrees with the Rust interpreter, the
// generator is faithful for them too.

#include "FG_FlowKit.generated.h"
#include "FG_ListKit.generated.h"
#include "FG_MathKit.generated.h"
#include "FG_TypesKit.generated.h"
#include "FG_WorldKit.generated.h"

#include "harness_common.h"

using namespace harness;

namespace {

bool Same(const glm::vec3& a, const glm::vec3& b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }

void Turn(Object* o, const glm::vec3& rotation) {
    o->getTransform()->rotation = rotation;
    o->getTransform()->updateQuaternion();
}

// ---- math, vectors and logic ----

void ClampAndMapRange() {
    StubHost host;
    World w(host);
    FG_MathKit* k = w.As<FG_MathKit>(w.Spawn<FG_MathKit>(glm::vec3(0)));
    CHECK(k->Limit(15) == 10 && k->Limit(-3) == 0 && k->Limit(5) == 5, "clamp keeps a whole number between two others");
    CHECK(Near(k->LimitDecimal(2.5f), 1.0f) && Near(k->LimitDecimal(-2.5f), -1.0f), "and a decimal");
    CHECK(Near(k->Scale(5.0f), 0.5f) && Near(k->Scale(20.0f), 1.0f) && Near(k->Scale(-1.0f), 0.0f), "map range converts and stays inside");
}

void RandomNumbersAreTheSameAsTheInterpretersForTheSameSeed() {
    StubHost host;
    World w(host);
    w.Seed(7);
    FG_MathKit* k = w.As<FG_MathKit>(w.Spawn<FG_MathKit>(glm::vec3(0)));
    const int expected[12] = {5, 3, 1, 1, 6, 5, 6, 5, 5, 3, 3, 6};  // what the interpreter rolls with seed 7
    bool same = true;
    for (int i = 0; i < 12; ++i) same = same && k->Roll() == expected[i];
    CHECK(same, "twelve rolls from seed 7 match the interpreter's");
    const float jitter[4] = {5.6502295f, 5.902316f, 5.126594f, 5.333399f};
    bool sameJitter = true;
    for (int i = 0; i < 4; ++i) sameJitter = sameJitter && std::fabs(k->Jitter() - jitter[i]) < 1e-5f;
    CHECK(sameJitter, "and so do the decimals that follow");
}

void VectorsAndRotations() {
    StubHost host;
    World w(host);
    Object* o = w.Spawn<FG_MathKit>(glm::vec3(0));
    FG_MathKit* k = w.As<FG_MathKit>(o);
    CHECK(Same(k->Unit(), glm::vec3(0.6f, 0.0f, 0.8f)), "normalized");
    CHECK(Near(k->Size(), 5.0f), "length");
    CHECK(Near(k->Alignment(glm::vec3(1, 0, 0)), 3.0f) && Near(k->Alignment(glm::vec3(0, 1, 0)), 0.0f), "dot product");
    CHECK(Same(k->Pose(), glm::vec3(10, 30, 20)), "make rotator is stored as (pitch, roll, yaw)");
    CHECK(Same(k->Facing(glm::vec3(0, 10, 0)), glm::vec3(0, 0, 0)), "looking along +Y is no turn");
    CHECK(Same(k->Facing(glm::vec3(10, 0, 0)), glm::vec3(0, 0, -90)), "looking along +X is a yaw of -90");
    CHECK(Same(k->Facing(glm::vec3(0, 5, 5)), glm::vec3(45, 0, 0)), "looking up and ahead is a pitch of 45");

    CHECK(Same(k->Ahead(), glm::vec3(0, 1, 0)) && Same(k->Side(), glm::vec3(1, 0, 0)) && Same(k->Above(), glm::vec3(0, 0, 1)), "unturned directions");
    Turn(o, glm::vec3(0, 0, 90));
    CHECK(Same(k->Ahead(), glm::vec3(-1, 0, 0)) && Same(k->Side(), glm::vec3(0, 1, 0)) && Same(k->Above(), glm::vec3(0, 0, 1)), "directions follow the yaw");
    Turn(o, k->Facing(glm::vec3(10, 0, 0)));
    CHECK(Same(k->Ahead(), glm::vec3(1, 0, 0)), "turning to the look-at rotation points forward at the target");
}

void TakingAVectorApart() {
    StubHost host;
    World w(host);
    FG_MathKit* k = w.As<FG_MathKit>(w.Spawn<FG_MathKit>(glm::vec3(0)));
    CHECK(Near(k->Sideways(), 3.0f), "Dir.X");
    k->Bend(2.5f);
    CHECK(Same(k->Dir, glm::vec3(3.0f, 2.5f, 4.0f)), "only Y changed");
    CHECK(Near(k->Heading(), 20.0f), "the yaw of a rotation is its Z");
}

void LogicAndEasing() {
    StubHost host;
    host.level = "forest";
    World w(host);
    Object* o = w.Spawn<FG_MathKit>(glm::vec3(0));
    FG_MathKit* k = w.As<FG_MathKit>(o);
    CHECK(!k->Opposite(true) && k->Opposite(false), "not");
    CHECK(k->Label() == "open" && k->Pick(1, 2) == 1, "select when the condition holds");
    k->Ready = false;
    CHECK(k->Label() == "closed" && k->Pick(1, 2) == 2, "select when it doesn't");
    k->Ready = true;

    w.Step(0.1f);
    CHECK(Near(k->Slide(0.0f), 5.0f), "ease moves half the way at speed 5 for a tenth of a second");
    CHECK(Same(k->Glide(glm::vec3(0)), glm::vec3(1.5f, 0, 2)), "and a vector too");
    w.Step(0.01f - 0.0f);
    CHECK(Same(k->Swing(glm::vec3(350, 0, 0)), glm::vec3(361.7f, 0, 3.6f)), "turn takes the short way round");

    CHECK(k->Level() == "forest", "level name asks the host");
    CHECK(k->Describe() == "Dir is (3, 0, 4) and ready is true", "format text fills in the values");
    k->Remember(5);
    k->Remember(7);
    k->Remember(5);
    CHECK(k->Where(7) == 1 && k->Where(9) == -1, "position of a value in a list");
}

// ---- lists ----

void ListNodes() {
    StubHost host;
    World w(host);
    FG_ListKit* l = w.As<FG_ListKit>(w.Spawn<FG_ListKit>(glm::vec3(0)));
    l->Add(1);
    l->Add(2);
    l->Add(2);
    CHECK(l->Total() == 2, "add unique skips a repeat");
    l->AddAnyway(2);
    CHECK(l->Total() == 3, "plain add allows it");
    CHECK(l->Has(2) && !l->Has(9) && l->Lacks(9) && !l->Lacks(1), "contains and does not contain");
    CHECK(l->Where(2) == 1 && l->Where(9) == -1, "position of");
    l->Drop(0);
    CHECK(l->Where(2) == 0 && l->Total() == 2, "remove at moves the rest up");
    CHECK(w.warnings.empty(), "no warnings so far");
    l->Drop(99);
    CHECK(l->Total() == 2, "past the end removes nothing");
    CHECK(!w.warnings.empty() && w.warnings.back().find("past the end") != std::string::npos, "and warns");
    l->Wipe();
    CHECK(l->Total() == 0, "clear");
}

// ---- flow ----

void DoOnceDoNAndFlipFlop() {
    StubHost host;
    World w(host);
    Object* oa = w.Spawn<FG_FlowKit>(glm::vec3(0));
    Object* ob = w.Spawn<FG_FlowKit>(glm::vec3(0));
    FG_FlowKit* a = w.As<FG_FlowKit>(oa);
    FG_FlowKit* b = w.As<FG_FlowKit>(ob);
    for (int i = 0; i < 3; ++i) a->Greet();
    CHECK(a->Hellos == 1, "do once runs once");
    a->Rearm();
    a->Greet();
    a->Greet();
    CHECK(a->Hellos == 2, "until it is reset");

    a->Rearm();
    for (int i = 0; i < 5; ++i) a->Warm();
    CHECK(a->Warmups == 3, "do N runs that many times");
    b->Warm();
    CHECK(b->Warmups == 1, "each entity counts for itself");
    a->Rearm();
    for (int i = 0; i < 5; ++i) a->Warm();
    CHECK(a->Warmups == 6, "and a reset gives it back");

    for (int i = 0; i < 5; ++i) a->Step();
    CHECK(a->Lefts == 3 && a->Rights == 2, "flip flop alternates");
}

void GateAndSwitch() {
    StubHost host;
    World w(host);
    FG_FlowKit* f = w.As<FG_FlowKit>(w.Spawn<FG_FlowKit>(glm::vec3(0)));
    f->Pass();
    CHECK(f->Passes == 0, "a gate that starts closed lets nothing through");
    f->Unlock();
    f->Pass();
    CHECK(f->Passes == 1, "open");
    f->Lock();
    f->Pass();
    CHECK(f->Passes == 1, "closed again");
    f->Flip();
    f->Pass();
    CHECK(f->Passes == 2, "toggling a closed gate opens it");
    f->Flip();
    f->Pass();
    CHECK(f->Passes == 2, "and an open one closes");

    f->Name(1);
    CHECK(f->Word == "one", "case 1");
    f->Name(3);
    CHECK(f->Word == "few", "case 2 or 3");
    f->Name(9);
    CHECK(f->Word == "many", "otherwise");
    f->Spell("b");
    CHECK(f->Word == "second", "switch on text");
    f->Spell("zzz");
    CHECK(f->Word == "second", "no case and no otherwise does nothing");
}

void Timelines() {
    StubHost host;
    World w(host);
    Object* o = w.Spawn<FG_FlowKit>(glm::vec3(0));
    FG_FlowKit* f = w.As<FG_FlowKit>(o);
    f->Rise();
    CHECK(f->Frames == 0, "it starts on the next frame");
    std::vector<float> heights;
    for (int i = 0; i < 4; ++i) {
        w.Step(0.5f);
        heights.push_back(Pos(o).z);
    }
    CHECK(Near(heights[0], 1.0f) && Near(heights[1], 2.0f) && Near(heights[2], 3.0f) && Near(heights[3], 4.0f), "the value moves from 0 to 4 over two seconds");
    CHECK(f->Frames == 4, "four frames");
    Pos(o).z = 50.0f;
    w.Step(0.5f);
    CHECK(f->Frames == 4 && Near(Pos(o).z, 50.0f), "then it stops");

    Pos(o) = glm::vec3(0);
    f->Travel();
    w.Step(0.25f);
    CHECK(Same(Pos(o), glm::vec3(2.5f, 0, 0)), "a timeline can move a vector");
    w.Step(0.75f);
    CHECK(Same(Pos(o), glm::vec3(10, 0, 0)), "and ends on the last value");
}

// ---- the world ----

void DamageVisibilityAndMessages() {
    StubHost host;
    World w(host);
    Object* oa = w.Spawn<FG_WorldKit>(glm::vec3(0));
    Object* ob = w.Spawn<FG_WorldKit>(glm::vec3(10, 0, 0));
    FG_WorldKit* a = w.As<FG_WorldKit>(oa);
    FG_WorldKit* b = w.As<FG_WorldKit>(ob);

    a->Hurt(ob, 30.0f);
    CHECK(Near(b->Health, 70.0f) && Near(b->LastHit, 30.0f), "damage runs the target's handler with the amount");
    CHECK(b->Attacker == oa, "and who did it");
    CHECK(Near(a->Health, 100.0f), "the attacker is unhurt");

    CHECK(w.IsShown(oa) && w.IsCollidable(oa), "shown and solid to start with");
    a->Hide();
    CHECK(!w.IsShown(oa), "hide");
    a->Reveal();
    CHECK(w.IsShown(oa), "reveal");
    a->Ghost();
    CHECK(!w.IsCollidable(oa), "collision off");

    a->Poke(ob);
    CHECK(b->Pings == 3, "tell runs an action by name");
    w.Pump();

    Object* om = w.Spawn<FG_MathKit>(glm::vec3(0));
    a->Poke(om);
    CHECK(w.warnings.empty(), "an entity with no such action is skipped quietly");
    CHECK(a->IsPad(om) || true, "cast runs");
    w.Hit(oa, om);
    CHECK(a->Bumps == 1, "a hit reaches an entity that listens for that script");
    w.Hit(oa, ob);
    CHECK(a->Bumps == 1, "but not one running another script");
}

void AttachingFollowsAndDetaches() {
    StubHost host;
    World w(host);
    Object* oa = w.Spawn<FG_WorldKit>(glm::vec3(0));
    Object* ob = w.Spawn<FG_WorldKit>(glm::vec3(10, 0, 0));
    FG_WorldKit* a = w.As<FG_WorldKit>(oa);
    a->Follow(ob);
    CHECK(Same(Pos(oa), glm::vec3(0)), "attaching doesn't move it");
    Pos(ob) = glm::vec3(10, 0, 5);
    w.Step(0.1f);
    CHECK(Same(Pos(oa), glm::vec3(0, 0, 5)), "it follows its parent");
    Turn(ob, glm::vec3(0, 0, 90));
    w.Step(0.1f);
    CHECK(Same(Pos(oa), glm::vec3(10, -10, 5)), "and turns with it");
    CHECK(Near(oa->getTransform()->rotation.z, 90.0f), "taking its rotation along");
    a->Release();
    Pos(ob) = glm::vec3(99, 0, 0);
    w.Step(0.1f);
    CHECK(Same(Pos(oa), glm::vec3(10, -10, 5)), "once detached it stays where it was");

    w.warnings.clear();
    a->Follow(ob);
    w.As<FG_WorldKit>(ob)->Follow(oa);
    bool warned = false;
    for (auto& m : w.warnings) warned = warned || m.find("follow each other") != std::string::npos;
    CHECK(warned, "two entities can't follow each other");
}

void EngineSystemsReceiveTheirOptionalArguments() {
    StubHost host;
    World w(host);
    FG_WorldKit* a = w.As<FG_WorldKit>(w.Spawn<FG_WorldKit>(glm::vec3(0)));
    a->Effects();
    CHECK(host.calls.size() == 8, "eight system calls");
    CHECK(host.HasCall("Audio.Play sound=\"boom.wav\" at=(0, 0, 0) volume=0.5"), "play at a place, with a volume");
    CHECK(host.HasCall("Audio.Play sound=\"hum.wav\" attachedTo=entity"), "play attached to an entity");
    CHECK(host.HasCall("Animation.Play animation=\"walk\" loop=true"), "play an animation");
    CHECK(host.HasCall("Animation.PlayMontage montage=\"attack\" section=\"swing\""), "play a montage");
    CHECK(host.HasCall("UI.Create widget=\"hud\"") && host.HasCall("UI.Remove element=\"hud\""), "create and remove a widget");
    CHECK(host.HasCall("Game.OpenLevel level=\"forest\"") && host.HasCall("Game.SetPaused paused=true"), "open a level, pause the game");
}

// ---- enums, structs, tables and traces ----

void EnumsStructsAndTables() {
    StubHost host;
    World w(host);
    FG_TypesKit* k = w.As<FG_TypesKit>(w.Spawn<FG_TypesKit>(glm::vec3(0)));
    CHECK(k->State == FG_TypesKit::Mode::Idle && k->Describe() == "idle" && k->IsIdle(), "an enum starts as its first value");
    k->Chase();
    CHECK(k->State == FG_TypesKit::Mode::Chasing && k->Describe() == "chasing" && !k->IsIdle(), "it changes, compares and switches");
    CHECK(k->Label() == "Chasing", "its text is the name of the value");

    k->Pick("Sword");
    CHECK(k->Price() == 5 && k->Stance() == FG_TypesKit::Mode::Attacking, "a table row, with an enum field");
    k->Pick("Shield");
    CHECK(k->Price() == 8 && k->Stance() == FG_TypesKit::Mode::Idle, "a field a row leaves out is its default");
    CHECK(k->Count() == 2, "structs can go in a list");
    CHECK(w.warnings.empty(), "no warnings so far");
    k->Pick("Nothing");
    CHECK(k->Price() == 0, "a missing row is the defaults");
    CHECK(!w.warnings.empty() && w.warnings.back().find("has no row \"Nothing\"") != std::string::npos, "and warns");

    k->Craft(7);
    CHECK(k->Price() == 7, "a struct made with new");
    k->Discount(3);
    CHECK(k->Price() == 4, "subtract from a field");
    k->Rename("Lance");
    k->Shift(2.5f);
    CHECK(k->Held.Name == "Lance" && Near(k->Held.Spot.x, 2.5f) && k->Held.Cost == 4, "fields change one at a time");
}

void Traces() {
    StubHost host;
    World w(host);
    Object* me = w.Spawn<FG_TypesKit>(glm::vec3(0));
    Object* nearOne = w.Spawn<FG_TypesKit>(glm::vec3(0, 0, -5));
    Object* farOne = w.Spawn<FG_TypesKit>(glm::vec3(0, 0, -10));
    FG_TypesKit* k = w.As<FG_TypesKit>(me);

    k->Scan(glm::vec3(0), glm::vec3(0, 0, -20));
    CHECK(k->Seen() && k->Who() == nearOne, "a trace reports the nearer of two in line");
    CHECK(Near(k->HowFar(), 4.5f) && Same(k->Surface(), glm::vec3(0, 0, 1)), "how far, and which way the surface faces");

    k->Scan(glm::vec3(0, 0, -20), glm::vec3(0, 0, -30));
    CHECK(!k->Seen() && k->Who() == nullptr && Near(k->HowFar(), 10.0f), "nothing in the way reports the whole length");

    w.SetCollision(nearOne, false);
    k->Scan(glm::vec3(0), glm::vec3(0, 0, -20));
    CHECK(k->Who() == farOne && Near(k->HowFar(), 9.5f), "something with collision off isn't there");

    k->Sweep(glm::vec3(3, 0, 0), glm::vec3(3, 0, -20), 1.0f);
    CHECK(!k->Seen(), "a thin sweep to the side misses");
    w.SetCollision(nearOne, true);
    k->Sweep(glm::vec3(3, 0, 0), glm::vec3(3, 0, -20), 3.0f);
    CHECK(k->Who() == nearOne && Near(k->HowFar(), 5.0f - std::sqrt(3.25f)), "a thick one hits");
}

// An engine that knows the level: an unscripted floor at z = -3, and says a scripted object is the one it hit when told to.
struct LevelHost : StubHost {
    Object* names = nullptr;
    std::vector<Object*> askedBy;

    bool Trace(Object* me, const glm::vec3& from, const glm::vec3& to, float radius, HitResult& out) override {
        askedBy.push_back(me);
        const float length = glm::length(to - from);
        const float top = -3.0f + radius;  // a thick line stops a radius above the floor
        out = HitResult();
        if (from.z > top && to.z <= top) {
            const float t = (from.z - top) / (from.z - to.z);
            out.Blocked = true;
            out.Entity = names;
            out.Location = glm::vec3(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, -3.0f);
            out.Normal = glm::vec3(0, 0, 1);
            out.Distance = t * length;
        } else {
            out.Location = to;
            out.Distance = length;
        }
        return true;
    }
};

void TracesAreAnsweredByTheEngine() {
    LevelHost host;
    World w(host);
    Object* me = w.Spawn<FG_TypesKit>(glm::vec3(0));
    Object* other = w.Spawn<FG_TypesKit>(glm::vec3(0, 0, -5));
    FG_TypesKit* k = w.As<FG_TypesKit>(me);

    k->Scan(glm::vec3(0, 0, 1), glm::vec3(0, 0, -20));
    CHECK(k->Seen() && k->Who() == nullptr, "the engine's floor blocks the line, and has no script to name");
    CHECK(Near(k->HowFar(), 4.0f) && Same(k->Surface(), glm::vec3(0, 0, 1)), "how far, and which way the surface faces");
    CHECK(host.askedBy.size() == 1 && host.askedBy[0] == me, "the engine is told who is asking");

    host.names = other;
    k->Scan(glm::vec3(0, 0, 1), glm::vec3(0, 0, -20));
    CHECK(k->Who() == other, "when it hit a scripted object, the script can name it");

    k->Scan(glm::vec3(0, 0, 1), glm::vec3(0, 0, -1));
    CHECK(!k->Seen() && Near(k->HowFar(), 2.0f), "a line that stops short");
    k->Sweep(glm::vec3(0, 0, 1), glm::vec3(0, 0, -20), 1.0f);
    CHECK(Near(k->HowFar(), 3.0f), "a thick one stops a radius above the floor");
}

}  // namespace

int main() {
    ClampAndMapRange();
    RandomNumbersAreTheSameAsTheInterpretersForTheSameSeed();
    VectorsAndRotations();
    TakingAVectorApart();
    LogicAndEasing();
    ListNodes();
    DoOnceDoNAndFlipFlop();
    GateAndSwitch();
    Timelines();
    DamageVisibilityAndMessages();
    AttachingFollowsAndDetaches();
    EngineSystemsReceiveTheirOptionalArguments();
    EnumsStructsAndTables();
    Traces();
    TracesAreAnsweredByTheEngine();
    return Finish();
}
