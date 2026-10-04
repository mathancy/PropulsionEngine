// differential_examples.cpp
// The same scenarios as forge/tests/runtime.rs (the example files), against generated C++.
// These cover what the behaviors don't: timers, entity lookup by tag, lists, events between
// scripts, and `when:startup` instances.

#include "FG_Coin.generated.h"
#include "FG_Enemy.generated.h"
#include "FG_Hud.generated.h"
#include "FG_Player.generated.h"
#include "FG_Spawner.generated.h"
#include "FG_Switch.generated.h"

#include "harness_common.h"

using namespace harness;

namespace {

int Count(World& w, auto&& isIt) {
    int n = 0;
    for (const auto& o : w.Objects()) {
        if (o->isActive() && isIt(o.get())) ++n;
    }
    return n;
}

void HoldingForwardMovesThePlayerFiveUnitsInOneSecond() {
    StubHost host;
    World w(host);
    Object* p = w.Spawn<FG_Player>(glm::vec3(0));
    host.Press("moveForward");
    Run(w, 1.0, 1.0f / 64.0f);
    CHECK(Near(Pos(p).z, -5.0f), "forward is -Z");
    CHECK(w.warnings.empty(), "no warnings");
}

void WalkingBackReversesIt() {
    StubHost host;
    World w(host);
    Object* p = w.Spawn<FG_Player>(glm::vec3(0));
    host.Press("moveBack");
    Run(w, 0.5, 1.0f / 64.0f);
    CHECK(Near(Pos(p).z, 2.5f), "back is +Z");
}

void TouchingACoinCollectsItAndUpdatesTheHud() {
    StubHost host;
    World w(host);
    Object* player = w.Spawn<FG_Player>(glm::vec3(0));
    Object* coin = w.Spawn<FG_Coin>(glm::vec3(1, 0, 0));
    w.Touch(player, coin);
    CHECK(w.As<FG_Player>(player)->Coins == 1, "one coin");
    CHECK(!coin->isActive(), "the coin is destroyed");
    CHECK(host.HasCall("UI.SetText element=\"coins\" text=\"Coins: 1\""), "the HUD shows the count");
    CHECK(Count(w, [&](Object* o) { return w.As<FG_Hud>(o) != nullptr; }) == 1, "the HUD exists exactly once (when:startup)");
}

void DamageIsFlooredAtZeroAndDeathRespawnsThePlayer() {
    StubHost host;
    World w(host);
    Object* o = w.Spawn<FG_Player>(glm::vec3(0));
    Pos(o) = glm::vec3(3, 2, 1);
    FG_Player* p = w.As<FG_Player>(o);

    p->TakeDamage(30);
    w.Pump();
    CHECK(p->Health == 70, "took 30");

    p->TakeDamage(500);
    w.Pump();
    CHECK(p->Health == 100, "respawn restores full health");
    CHECK(!p->IsInvincible, "invincibility ends after the flashes");
    CHECK(Pos(o) == glm::vec3(0, 1, 0), "respawn moves the player to Spawn");
    CHECK(host.CountCalls("UI.Play animation=\"flash\"") == 3, "three flashes");
    CHECK(host.HasCall("UI.SetText element=\"message\" text=\"You died\""), "death message");
}

void AnInvinciblePlayerTakesNoDamage() {
    StubHost host;
    World w(host);
    Object* o = w.Spawn<FG_Player>(glm::vec3(0));
    FG_Player* p = w.As<FG_Player>(o);
    p->IsInvincible = true;
    p->TakeDamage(50);
    CHECK(p->Health == 100, "no damage");
}

void HealthRegeneratesOnlyAfterTenQuietSeconds() {
    StubHost host;
    World w(host);
    Object* o = w.Spawn<FG_Player>(glm::vec3(0));
    FG_Player* p = w.As<FG_Player>(o);
    p->Health = 40;
    Run(w, 14.75, 0.25f);
    CHECK(p->Health == 40, "not healed yet at 14.75s");
    Run(w, 0.5, 0.25f);
    CHECK(p->Health == 90, "healed 50 at the 15s check");
    Run(w, 5.0, 0.25f);
    CHECK(p->Health == 100, "healing is capped at MaxHealth");
}

void TakingDamageRestartsTheRegenerationTimer() {
    StubHost host;
    World w(host);
    Object* o = w.Spawn<FG_Player>(glm::vec3(0));
    FG_Player* p = w.As<FG_Player>(o);
    p->Health = 40;
    Run(w, 12.0, 0.25f);
    p->TakeDamage(5);
    w.Pump();
    Run(w, 3.25, 0.25f);
    CHECK(p->Health == 35, "the 15s check saw only 3 quiet seconds");
}

void ACoinSpinsAndReportsItsValue() {
    StubHost host;
    World w(host);
    Object* o = w.Spawn<FG_Coin>(glm::vec3(0));
    FG_Coin* coin = w.As<FG_Coin>(o);
    Run(w, 1.0, 0.25f);
    CHECK(Near(o->getTransform()->rotation.y, 90.0f), "spun 90 degrees");
    CHECK(coin->GetValue() == 1, "value is 1");
    coin->Collect();
    CHECK(!o->isActive(), "collected");
    CHECK(host.calls.size() == 1 && host.calls[0] == "Audio.Play sound=\"coin.wav\"", "played the coin sound");
}

void AnEnemyInRangeChasesThenAttacksThePlayer() {
    StubHost host;
    World w(host);
    Object* player = w.Spawn<FG_Player>(glm::vec3(0));
    Object* enemy = w.Spawn<FG_Enemy>(glm::vec3(6, 0, 0));
    Run(w, 6.0, 0.25f);
    const float gap = Pos(enemy).x - Pos(player).x;
    CHECK(gap <= 1.51f && gap > 0.0f, "the enemy stops at attack range");
    const int health = w.As<FG_Player>(player)->Health;
    CHECK(health < 100 && health % 10 == 0, "and hits the player in steps of 10");
}

void AnEnemyWithNoPlayerDoesNothing() {
    StubHost host;
    World w(host);
    Object* enemy = w.Spawn<FG_Enemy>(glm::vec3(6, 0, 0));
    Run(w, 2.0, 0.25f);
    CHECK(Pos(enemy) == glm::vec3(6, 0, 0), "it didn't move");
    bool mentionsPlayer = false;
    for (auto& m : w.warnings) mentionsPlayer = mentionsPlayer || m.find("Player") != std::string::npos;
    CHECK(!mentionsPlayer, "the missing player is guarded, not warned about");
}

void TheSpawnerKeepsThreeEnemiesAlive() {
    StubHost host;
    World w(host);
    Object* spawnerObj = w.Spawn<FG_Spawner>(glm::vec3(0));
    FG_Spawner* spawner = w.As<FG_Spawner>(spawnerObj);
    auto isEnemy = [&](Object* o) { return w.As<FG_Enemy>(o) != nullptr; };

    CHECK(Count(w, isEnemy) == 3, "one enemy per spawn point");
    CHECK(spawner->Spawned == 3, "spawned 3");

    std::vector<Object*> enemies;
    for (const auto& o : w.Objects()) {
        if (w.As<FG_Enemy>(o.get())) enemies.push_back(o.get());
    }
    CHECK(Pos(enemies[0]) == glm::vec3(0, 0, 0), "first spawn point");
    CHECK(Pos(enemies[1]) == glm::vec3(10, 0, 5), "second spawn point");

    w.As<FG_Enemy>(enemies[0])->TakeDamage(100);
    w.Pump();
    CHECK(!enemies[0]->isActive(), "the enemy is gone");
    CHECK(spawner->Alive.size() == 2, "the spawner heard `Defeated` and dropped it from its list");

    Run(w, 10.0, 0.5f);
    CHECK(Count(w, isEnemy) == 3, "a replacement after 10 seconds");
    CHECK(spawner->Spawned == 4, "spawned 4 in total");
}

void TheSwitchTurnsItselfOff() {
    StubHost host;
    World w(host);
    Object* o = w.Spawn<FG_Switch>(glm::vec3(0));
    FG_Switch* s = w.As<FG_Switch>(o);
    s->Switch = true;
    w.Step(1.0f / 60.0f);
    CHECK(!s->Switch, "turned itself off");
}

}  // namespace

int main() {
    HoldingForwardMovesThePlayerFiveUnitsInOneSecond();
    WalkingBackReversesIt();
    TouchingACoinCollectsItAndUpdatesTheHud();
    DamageIsFlooredAtZeroAndDeathRespawnsThePlayer();
    AnInvinciblePlayerTakesNoDamage();
    HealthRegeneratesOnlyAfterTenQuietSeconds();
    TakingDamageRestartsTheRegenerationTimer();
    ACoinSpinsAndReportsItsValue();
    AnEnemyInRangeChasesThenAttacksThePlayer();
    AnEnemyWithNoPlayerDoesNothing();
    TheSpawnerKeepsThreeEnemiesAlive();
    TheSwitchTurnsItselfOff();
    return Finish();
}
