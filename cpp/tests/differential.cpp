// differential.cpp
// Runs the same scenarios as forge/tests/behaviors.rs, but against the generated C++ classes
// (built on the engine's real Object and TransformComponent). If this agrees with the Rust
// interpreter, the generator is faithful.

#include "FG_Coin.generated.h"
#include "FG_Door.generated.h"
#include "FG_Drifter.generated.h"
#include "FG_GameState.generated.h"
#include "FG_HealthPickup.generated.h"
#include "FG_MovingPlatform.generated.h"
#include "FG_Player.generated.h"
#include "FG_Rotator.generated.h"
#include "FG_Trap.generated.h"

#include "harness_common.h"

using namespace harness;

namespace {

void AnalogAxesMoveRelativeToFacing() {
    StubHost host;
    World w(host);
    Object* p = w.Spawn<FG_Player>(glm::vec3(0));

    host.axes["moveForward"] = 1.0f;
    Run(w, 1.0, 0.125f);
    CHECK(Near(Pos(p).z, -5.0f), "forward is -Z");

    host.axes["moveForward"] = 0.0f;
    Pos(p) = glm::vec3(0);
    p->getTransform()->rotation = glm::vec3(0, 90, 0);
    p->getTransform()->updateQuaternion();
    host.axes["moveForward"] = 1.0f;
    Run(w, 1.0, 0.125f);
    CHECK(Near(Pos(p).x, -5.0f) && Near(Pos(p).z, 0.0f), "after a 90 degree yaw, forward is -X");

    host.axes["moveForward"] = 0.0f;
    Pos(p) = glm::vec3(0);
    p->getTransform()->rotation = glm::vec3(0);
    p->getTransform()->updateQuaternion();
    host.axes["moveRight"] = 0.5f;
    Run(w, 1.0, 0.125f);
    CHECK(Near(Pos(p).x, 2.5f), "half-strength right moves at half speed");
}

void LookAxisTurnsThePlayer() {
    StubHost host;
    World w(host);
    Object* p = w.Spawn<FG_Player>(glm::vec3(0));
    host.axes["lookX"] = 1.0f;
    Run(w, 1.0, 0.125f);
    CHECK(Near(p->getTransform()->rotation.y, 90.0f), "yaw is 90 after one second");
}

void JumpFiresOncePerPress() {
    StubHost host;
    World w(host);
    Object* p = w.Spawn<FG_Player>(glm::vec3(0));
    FG_Player* player = w.As<FG_Player>(p);

    host.Press("jump");
    Run(w, 0.5, 0.125f);
    CHECK(player->JumpCount == 1, "a held key is one press");
    CHECK(Near(Pos(p).y, 2.0f), "jump moved up by 2");
    CHECK(host.CountCalls("Audio.Play sound=\"jump.wav\"") == 1, "one jump sound");
    CHECK(host.printed.size() == 1 && host.printed[0] == "Jumped 1", "printed Jumped 1");

    host.Release("jump");
    w.Step(0.125f);
    host.Press("jump");
    w.Step(0.125f);
    CHECK(player->JumpCount == 2, "a second press");
    CHECK(host.printed.back() == "Jumped 2", "printed Jumped 2");
}

void DoorOpensAndClosesAndPlaysItsSoundOncePerChange() {
    StubHost host;
    World w(host);
    Object* door = w.Spawn<FG_Door>(glm::vec3(0));
    Object* player = w.Spawn<FG_Player>(glm::vec3(0));
    FG_Door* d = w.As<FG_Door>(door);

    w.Touch(door, player);
    CHECK(d->IsOpen, "opens on overlap begin");
    CHECK(host.CountCalls("Audio.Play sound=\"door.wav\"") == 1, "one sound when it starts to open");

    Run(w, 1.0, 0.125f);
    CHECK(Near(d->Progress, 1.0f), "fully open");
    CHECK(Near(Pos(door).y, 3.0f), "open height is 3");
    CHECK(host.CountCalls("Audio.Play sound=\"door.wav\"") == 1, "no repeat while it stays open");

    w.Untouch(door, player);
    CHECK(!d->IsOpen, "closes on overlap end");
    CHECK(host.CountCalls("Audio.Play sound=\"door.wav\"") == 2, "one more when it starts to close");
    Run(w, 1.0, 0.125f);
    CHECK(Near(Pos(door).y, 0.0f), "closed height is 0");
}

void ExposedSettingsOnAPlacedDoor() {
    StubHost host;
    World w(host);
    Object* door = w.Spawn<FG_Door>(glm::vec3(0), {Arg{"OpenHeight", 5.0f}, Arg{"OpenSpeed", 10.0f}});
    Object* player = w.Spawn<FG_Player>(glm::vec3(0));
    w.Touch(door, player);
    Run(w, 0.25, 0.125f);
    CHECK(Near(Pos(door).y, 5.0f), "placed door opens to its own height");
    CHECK(w.warnings.empty(), "no warnings");

    StubHost host2;
    World w2(host2);
    Object* d2 = w2.Spawn<FG_Door>(glm::vec3(0), {Arg{"IsOpen", true}});
    CHECK(!w2.As<FG_Door>(d2)->IsOpen, "a variable that isn't exposed is not set");
    bool warned = false;
    for (auto& m : w2.warnings) warned = warned || m.find("isn't exposed") != std::string::npos;
    CHECK(warned, "and it warns");
}

void PickupHealsAndDisappears() {
    StubHost host;
    World w(host);
    Object* pickup = w.Spawn<FG_HealthPickup>(glm::vec3(0));
    Object* player = w.Spawn<FG_Player>(glm::vec3(0));
    FG_Player* p = w.As<FG_Player>(player);
    p->Health = 50;

    w.Touch(pickup, player);
    CHECK(p->Health == 75, "healed by 25");
    CHECK(!pickup->isActive(), "pickup is gone");
    CHECK(host.CountCalls("Audio.Play sound=\"pickup.wav\"") == 1, "pickup sound");

    p->Health = 90;
    Object* again = w.Spawn<FG_HealthPickup>(glm::vec3(0));
    w.Touch(again, player);
    CHECK(p->Health == 100, "healing is capped at MaxHealth");
}

void RotatorUsesExposedSpeed() {
    StubHost host;
    World w(host);
    Object* slow = w.Spawn<FG_Rotator>(glm::vec3(0));
    Object* fast = w.Spawn<FG_Rotator>(glm::vec3(0), {Arg{"SpinSpeed", 90.0f}});
    Run(w, 2.0, 0.25f);
    CHECK(Near(slow->getTransform()->rotation.y, 90.0f), "slow turned 90");
    CHECK(Near(fast->getTransform()->rotation.y, 180.0f), "fast turned 180");
    CHECK(host.printed.size() == 2, "the 2-second print fired once for each");
    CHECK(!host.printed.empty() && host.printed[0] == "Rotation is 90", "and it printed the rotation");
}

void PlatformMovesBackAndForth() {
    StubHost host;
    World w(host);
    Object* p = w.Spawn<FG_MovingPlatform>(glm::vec3(0));
    Run(w, 2.0, 0.25f);
    CHECK(Near(Pos(p).z, 5.0f), "halfway out");
    Run(w, 2.0, 0.25f);
    CHECK(Near(Pos(p).z, 10.0f), "at the far point");
    CHECK(w.As<FG_MovingPlatform>(p)->Returning, "now returning");
    Run(w, 2.0, 0.25f);
    CHECK(Near(Pos(p).z, 5.0f), "halfway back");
    Run(w, 2.0, 0.25f);
    CHECK(Near(Pos(p).z, 0.0f), "home again");
}

void DrifterScalesVectorsPerSecond() {
    StubHost host;
    World w(host);
    Object* a = w.Spawn<FG_Drifter>(glm::vec3(0));
    Object* b = w.Spawn<FG_Drifter>(glm::vec3(0), {Arg{"Direction", glm::vec3(0, 1, 0)}, Arg{"Speed", 4.0f}});
    Run(w, 1.0, 0.125f);
    CHECK(Near(Pos(a).x, 2.0f), "default drift");
    CHECK(Near(Pos(b).y, 4.0f), "custom drift");
}

void TrapDelaysThenHurtsAndSpawnsCoinsWithAnIndex() {
    StubHost host;
    World w(host);
    Object* trap = w.Spawn<FG_Trap>(glm::vec3(0));
    Object* player = w.Spawn<FG_Player>(glm::vec3(0));
    FG_Player* p = w.As<FG_Player>(player);

    w.Touch(trap, player);
    CHECK(host.printed.size() == 1 && host.printed[0] == "Trap triggered", "triggered");
    Run(w, 1.5, 0.5f);
    CHECK(p->Health == 100, "nothing before the two seconds are up");
    Run(w, 0.5, 0.5f);
    CHECK(p->Health == 80, "the delayed block still knew who the victim was");
    CHECK(host.printed.back() == "Trap sprung", "sprung");

    std::vector<float> xs;
    for (const auto& o : w.Objects()) {
        if (w.As<FG_Coin>(o.get())) xs.push_back(Pos(o.get()).x);
    }
    std::sort(xs.begin(), xs.end());
    CHECK(xs == std::vector<float>({0, 2, 4, 6, 8}), "five coins spaced by the loop index");

    w.Touch(trap, player);
    Run(w, 2.5, 0.5f);
    CHECK(p->Health == 80, "an armed trap does nothing more");
}

void GameStateCountsCoinsAndWins() {
    StubHost host;
    World w(host);
    Object* state = w.Spawn<FG_GameState>(glm::vec3(0));
    Object* player = w.Spawn<FG_Player>(glm::vec3(0));
    std::vector<Object*> coins;
    for (int i = 0; i < 3; ++i) coins.push_back(w.Spawn<FG_Coin>(glm::vec3(static_cast<float>(i), 0, 0)));

    for (int n = 0; n < 3; ++n) {
        w.Touch(coins[static_cast<size_t>(n)], player);
        CHECK(w.As<FG_GameState>(state)->Score == n + 1, "score goes up");
        CHECK(host.CountCalls("UI.SetText element=\"score\" text=\"Score: " + std::to_string(n + 1) + "\"") == 1,
              "the score text was updated through the variable change");
    }
    CHECK(host.CountCalls("UI.Show element=\"win\"") == 1, "only the third coin wins");
    CHECK(host.printed.size() == 1 && host.printed[0] == "You win!", "win message");
}

void PlayerDiesThroughAnActionAndItsEvent() {
    StubHost host;
    World w(host);
    Object* o = w.Spawn<FG_Player>(glm::vec3(0));
    FG_Player* p = w.As<FG_Player>(o);
    p->TakeDamage(40);
    w.Pump();
    CHECK(p->Health == 60, "took 40");
    p->TakeDamage(500);
    w.Pump();
    CHECK(p->Health == 0, "floored at zero");
    CHECK(host.printed.size() == 1 && host.printed[0] == "The player died", "died");
    CHECK(host.CountCalls("UI.Show element=\"game over\"") == 1, "game over shown");
}

}  // namespace

int main() {
    AnalogAxesMoveRelativeToFacing();
    LookAxisTurnsThePlayer();
    JumpFiresOncePerPress();
    DoorOpensAndClosesAndPlaysItsSoundOncePerChange();
    ExposedSettingsOnAPlacedDoor();
    PickupHealsAndDisappears();
    RotatorUsesExposedSpeed();
    PlatformMovesBackAndForth();
    DrifterScalesVectorsPerSecond();
    TrapDelaysThenHurtsAndSpawnsCoinsWithAnIndex();
    GameStateCountsCoinsAndWins();
    PlayerDiesThroughAnActionAndItsEvent();
    return Finish();
}
