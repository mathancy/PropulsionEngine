//! Run the example files in a world with a stub engine and check what they do.

use propulsion::runtime::{StubHost, Val, World};
use propulsion::Project;
use std::path::PathBuf;

fn examples() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join("examples")
}

fn world(entry: &str) -> World<StubHost> {
    let project = Project::load(&examples().join(entry)).unwrap();
    World::new(project, StubHost::default()).unwrap_or_else(|reports| {
        let shown: String = reports
            .iter()
            .flat_map(|r| r.errors.iter().map(move |e| e.render(&r.source, &r.path)))
            .collect();
        panic!("examples should pass the checker:\n{shown}")
    })
}

fn int(w: &World<StubHost>, id: usize, var: &str) -> i32 {
    match w.get(id, var) {
        Some(Val::Int(i)) => *i,
        other => panic!("{var} is not an integer: {other:?}"),
    }
}

fn near(a: f32, b: f32) -> bool {
    (a - b).abs() < 0.01
}

fn run(w: &mut World<StubHost>, seconds: f64, dt: f64) {
    let steps = (seconds / dt).round() as usize;
    for _ in 0..steps {
        w.step(dt);
    }
}

#[test]
fn holding_forward_moves_the_player_five_units_in_one_second() {
    let mut w = world("player.prop");
    let player = w.spawn_entry([0.0; 3]);
    w.host.press("moveForward");
    run(&mut w, 1.0, 1.0 / 64.0);
    // Forward is +Y in the engine (Z is up).
    assert!(near(w.entities[player].loc[1], 5.0), "y was {}", w.entities[player].loc[1]);
    assert!(w.warnings.is_empty(), "{:?}", w.warnings);
}

#[test]
fn walking_back_reverses_it() {
    let mut w = world("player.prop");
    let player = w.spawn_entry([0.0; 3]);
    w.host.press("moveBack");
    run(&mut w, 0.5, 1.0 / 64.0);
    assert!(near(w.entities[player].loc[1], -2.5), "y was {}", w.entities[player].loc[1]);
}

#[test]
fn touching_a_coin_collects_it_and_updates_the_hud() {
    let mut w = world("player.prop");
    let coin_key = w.file_key("coin.prop").unwrap();
    let player = w.spawn_entry([0.0; 3]);
    let coin = w.spawn(&coin_key, [1.0, 0.0, 0.0]);

    w.touch(player, coin);

    assert_eq!(int(&w, player, "Coins"), 1);
    assert!(!w.is_alive(coin), "the coin should be destroyed");
    assert!(
        w.host.calls.contains(&"UI.SetText element=\"coins\" text=\"Coins: 1\"".to_string()),
        "calls were {:?}",
        w.host.calls
    );
    // `ui/hud.prop` was needed by the player, so it exists as one entity.
    assert_eq!(w.entities.iter().filter(|e| e.file.ends_with("hud.prop")).count(), 1);
}

#[test]
fn damage_is_floored_at_zero_and_death_respawns_the_player() {
    let mut w = world("player.prop");
    let player = w.spawn_entry([0.0; 3]);
    w.entities[player].loc = [3.0, 2.0, 1.0];

    w.call(player, "TakeDamage", vec![("amount".into(), Val::Int(30))]);
    assert_eq!(int(&w, player, "Health"), 70);

    // Lethal damage: Health floors at 0, `Died` is sent, and its handler respawns the player.
    w.call(player, "TakeDamage", vec![("amount".into(), Val::Int(500))]);
    assert_eq!(int(&w, player, "Health"), 100, "respawn restores full health");
    assert_eq!(w.get(player, "IsInvincible"), Some(&Val::Bool(false)));
    assert_eq!(w.entities[player].loc, [0.0, 0.0, 1.0], "respawn moves the player to Spawn");

    let flashes = w.host.calls.iter().filter(|c| c.as_str() == "UI.Play animation=\"flash\"").count();
    assert_eq!(flashes, 3, "calls were {:?}", w.host.calls);
    assert!(w.host.calls.contains(&"UI.SetText element=\"message\" text=\"You died\"".to_string()));
}

#[test]
fn an_invincible_player_takes_no_damage() {
    let mut w = world("player.prop");
    let player = w.spawn_entry([0.0; 3]);
    w.set(player, "IsInvincible", Val::Bool(true));
    w.call(player, "TakeDamage", vec![("amount".into(), Val::Int(50))]);
    assert_eq!(int(&w, player, "Health"), 100);
}

#[test]
fn health_regenerates_only_after_ten_quiet_seconds() {
    let mut w = world("player.prop");
    let player = w.spawn_entry([0.0; 3]);
    w.set(player, "Health", Val::Int(40));

    // The regeneration check runs every 5 seconds and needs SinceDamage above 10 seconds.
    run(&mut w, 14.75, 0.25);
    assert_eq!(int(&w, player, "Health"), 40, "not healed yet at 14.75s");
    run(&mut w, 0.5, 0.25);
    assert_eq!(int(&w, player, "Health"), 90, "healed 50 at the 15s check");
    run(&mut w, 5.0, 0.25);
    assert_eq!(int(&w, player, "Health"), 100, "healing is capped at MaxHealth");
}

#[test]
fn taking_damage_restarts_the_regeneration_timer() {
    let mut w = world("player.prop");
    let player = w.spawn_entry([0.0; 3]);
    w.set(player, "Health", Val::Int(40));
    run(&mut w, 12.0, 0.25);
    w.call(player, "TakeDamage", vec![("amount".into(), Val::Int(5))]); // resets the timer at t=12
    run(&mut w, 3.25, 0.25); // t=15.25: the 15s check sees only 3 quiet seconds
    assert_eq!(int(&w, player, "Health"), 35);
}

#[test]
fn a_coin_spins_and_reports_its_value() {
    let mut w = world("coin.prop");
    let coin = w.spawn_entry([0.0; 3]);
    run(&mut w, 1.0, 0.25);
    assert!(near(w.entities[coin].rot[2], 90.0), "spin was {}", w.entities[coin].rot[2]);
    assert_eq!(w.call(coin, "GetValue", vec![]), Some(Val::Int(1)));
    w.call(coin, "Collect", vec![]);
    assert!(!w.is_alive(coin));
    assert_eq!(w.host.calls, vec!["Audio.Play sound=\"coin.wav\"".to_string()]);
}

#[test]
fn an_enemy_in_range_chases_then_attacks_the_player() {
    // The enemy file needs the player file, so loading from the enemy brings in both.
    let mut w = world("enemy.prop");
    let player_key = w.file_key("player.prop").unwrap();
    let player = w.spawn(&player_key, [0.0; 3]);
    let enemy = w.spawn_entry([6.0, 0.0, 0.0]);

    run(&mut w, 6.0, 0.25);

    let gap = w.entities[enemy].loc[0] - w.entities[player].loc[0];
    assert!(gap <= 1.5 + 0.01 && gap > 0.0, "enemy should stop at attack range, gap was {gap}");
    let health = int(&w, player, "Health");
    assert!(health < 100 && health % 10 == 0, "the enemy should have hit the player, health was {health}");
}

#[test]
fn an_enemy_with_no_player_does_nothing() {
    let mut w = world("enemy.prop");
    let enemy = w.spawn_entry([6.0, 0.0, 0.0]);
    run(&mut w, 2.0, 0.25);
    assert_eq!(w.entities[enemy].loc, [6.0, 0.0, 0.0]);
    // Only the empty waypoint list at creation produces a warning; the missing player is guarded.
    assert!(w.warnings.iter().all(|m| !m.contains("Player")), "{:?}", w.warnings);
}

#[test]
fn the_spawner_keeps_three_enemies_alive() {
    let mut w = world("spawner.prop");
    let spawner = w.spawn_entry([0.0; 3]);

    let alive = |w: &World<StubHost>| w.entities.iter().filter(|e| e.alive && e.file.ends_with("enemy.prop")).count();
    assert_eq!(alive(&w), 3, "one enemy per spawn point");
    assert_eq!(int(&w, spawner, "Spawned"), 3);
    let first = w.entities.iter().find(|e| e.file.ends_with("enemy.prop")).unwrap();
    assert_eq!(first.loc, [0.0, 0.0, 0.0]);
    assert_eq!(w.entities.iter().filter(|e| e.file.ends_with("enemy.prop")).nth(1).unwrap().loc, [10.0, 5.0, 0.0]);

    // Kill one enemy: it sends `Defeated`, the spawner removes it from its list...
    let victim = w.entities.iter().find(|e| e.file.ends_with("enemy.prop")).unwrap().id;
    w.call(victim, "TakeDamage", vec![("amount".into(), Val::Int(100))]);
    assert!(!w.is_alive(victim));
    match w.get(spawner, "Alive") {
        Some(Val::List(_, items)) => assert_eq!(items.len(), 2),
        other => panic!("{other:?}"),
    }

    // ...and the 10 second check spawns a replacement at the first spawn point.
    run(&mut w, 10.0, 0.5);
    assert_eq!(alive(&w), 3);
    assert_eq!(int(&w, spawner, "Spawned"), 4);
}

#[test]
fn the_switch_turns_itself_off() {
    let mut w = world("switch.prop");
    let id = w.spawn_entry([0.0; 3]);
    w.set(id, "Switch", Val::Bool(true));
    w.step(1.0 / 60.0);
    assert_eq!(w.get(id, "Switch"), Some(&Val::Bool(false)));
}

#[test]
fn scripts_cannot_run_a_project_that_fails_checking() {
    let dir = std::env::temp_dir().join("propulsion_runtime_bad");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("bad.prop");
    std::fs::write(&path, "header:\n\tvariable name:Health type:integer\n\nimplement:\n\tevery frame:\n\t\tmake Health \"full\"\n").unwrap();
    let project = Project::load(&path).unwrap();
    assert!(World::new(project, StubHost::default()).is_err());
}
