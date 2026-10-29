//! The behaviors suite: one file per node or example in the engine's design doc (section 6.4),
//! run in a world with a stub engine to check they do what the node would.

use propulsion::runtime::{StubHost, Val, World};
use propulsion::Project;
use std::path::PathBuf;

fn behaviors() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join("behaviors")
}

fn world(entry: &str) -> World<StubHost> {
    let project = Project::load(&behaviors().join(entry)).unwrap();
    World::new(project, StubHost::default()).unwrap_or_else(|reports| {
        let shown: String = reports
            .iter()
            .flat_map(|r| r.errors.iter().map(move |e| e.render(&r.source, &r.path)))
            .collect();
        panic!("behaviors should pass the checker:\n{shown}")
    })
}

fn run(w: &mut World<StubHost>, seconds: f64, dt: f64) {
    for _ in 0..(seconds / dt).round() as usize {
        w.step(dt);
    }
}

fn near(a: f32, b: f32) -> bool {
    (a - b).abs() < 0.01
}

fn int(w: &World<StubHost>, id: usize, var: &str) -> i32 {
    match w.get(id, var) {
        Some(Val::Int(i)) => *i,
        other => panic!("{var} is not an integer: {other:?}"),
    }
}

fn dec(w: &World<StubHost>, id: usize, var: &str) -> f32 {
    match w.get(id, var) {
        Some(Val::Dec(d)) => *d,
        other => panic!("{var} is not a decimal: {other:?}"),
    }
}

fn count_calls(w: &World<StubHost>, call: &str) -> usize {
    w.host.calls.iter().filter(|c| c.as_str() == call).count()
}

#[test]
fn every_behavior_parses_and_type_checks() {
    let mut files: Vec<_> = std::fs::read_dir(behaviors())
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|x| x == "prop"))
        .collect();
    files.sort();
    assert!(files.len() >= 9, "expected the behavior files, found {}", files.len());
    for path in files {
        let project = Project::load(&path).unwrap();
        for report in project.check(&propulsion::Manifest::builtin()) {
            let shown: String = report.errors.iter().map(|e| e.render(&report.source, &report.path)).collect();
            assert!(report.errors.is_empty(), "{} has problems:\n{shown}", report.path);
        }
    }
}

// ---- events and input ----

#[test]
fn analog_axes_move_the_player_relative_to_where_it_faces() {
    let mut w = world("player.prop");
    let p = w.spawn_entry([0.0; 3]);

    // Forward is +Y (the engine's convention: Z up, forward +Y, right +X).
    w.host.set_axis("moveForward", 1.0);
    run(&mut w, 1.0, 0.125);
    assert!(near(w.entities[p].loc[1], 5.0), "y was {}", w.entities[p].loc[1]);
    assert!(near(w.entities[p].loc[2], 0.0), "walking must stay level, but z was {}", w.entities[p].loc[2]);

    // Turn to face left: after a 90 degree yaw (about Z, the up axis), forward points along -X.
    w.host.set_axis("moveForward", 0.0);
    w.entities[p].loc = [0.0; 3];
    w.entities[p].rot = [0.0, 0.0, 90.0];
    w.host.set_axis("moveForward", 1.0);
    run(&mut w, 1.0, 0.125);
    assert!(near(w.entities[p].loc[0], -5.0) && near(w.entities[p].loc[1], 0.0), "loc was {:?}", w.entities[p].loc);

    // Right is +X when facing forward, and a half-strength axis moves at half speed.
    w.host.set_axis("moveForward", 0.0);
    w.entities[p].loc = [0.0; 3];
    w.entities[p].rot = [0.0; 3];
    w.host.set_axis("moveRight", 0.5);
    run(&mut w, 1.0, 0.125);
    assert!(near(w.entities[p].loc[0], 2.5), "loc was {:?}", w.entities[p].loc);
}

#[test]
fn the_look_axes_turn_the_player_the_way_the_mouse_moves() {
    let mut w = world("player.prop");
    let p = w.spawn_entry([0.0; 3]);

    // lookX is positive when the mouse moves right, and turning right is a negative yaw (positive turns left).
    w.host.set_axis("lookX", 1.0);
    run(&mut w, 1.0, 0.125);
    assert!(near(w.entities[p].rot[2], -90.0), "yaw was {}", w.entities[p].rot[2]);

    // So after a mouse-right turn, forward points along +X (the player's right).
    w.host.set_axis("lookX", 0.0);
    w.host.set_axis("moveForward", 1.0);
    w.entities[p].loc = [0.0; 3];
    run(&mut w, 1.0, 0.125);
    assert!(near(w.entities[p].loc[0], 5.0) && near(w.entities[p].loc[1], 0.0), "loc was {:?}", w.entities[p].loc);
}

#[test]
fn the_look_up_axis_tilts_the_player_and_stops_short_of_straight_up() {
    let mut w = world("player.prop");
    let p = w.spawn_entry([0.0; 3]);

    // lookY is positive when the mouse moves up, which tilts the view up (positive pitch, about X).
    w.host.set_axis("lookY", 0.5);
    run(&mut w, 0.5, 0.125);
    assert!(near(w.entities[p].rot[0], 22.5), "pitch was {}", w.entities[p].rot[0]);

    // Keep going: it stops at 89 degrees rather than flipping over the top.
    run(&mut w, 5.0, 0.125);
    assert!(near(w.entities[p].rot[0], 89.0), "pitch was {}", w.entities[p].rot[0]);

    // And the same looking down.
    w.host.set_axis("lookY", -1.0);
    run(&mut w, 5.0, 0.125);
    assert!(near(w.entities[p].rot[0], -89.0), "pitch was {}", w.entities[p].rot[0]);
}

#[test]
fn walking_stays_on_the_ground_while_looking_up_or_down() {
    let mut w = world("player.prop");
    let p = w.spawn_entry([0.0; 3]);
    w.host.set_axis("moveForward", 1.0);

    for pitch in [60.0, -60.0, 89.0] {
        w.entities[p].loc = [0.0; 3];
        w.entities[p].rot = [pitch, 0.0, 0.0];
        run(&mut w, 1.0, 0.125);
        // Full speed along the ground, whatever the pitch (looking steeply doesn't slow it), and no climbing.
        assert!(near(w.entities[p].loc[1], 5.0) && near(w.entities[p].loc[2], 0.0),
            "pitch {pitch}: loc was {:?}", w.entities[p].loc);
    }

    // Combined with a turn: yaw 90 faces -X, so a pitched walk goes along -X.
    w.entities[p].loc = [0.0; 3];
    w.entities[p].rot = [45.0, 0.0, 90.0];
    run(&mut w, 1.0, 0.125);
    assert!(near(w.entities[p].loc[0], -5.0) && near(w.entities[p].loc[1], 0.0) && near(w.entities[p].loc[2], 0.0),
        "loc was {:?}", w.entities[p].loc);

    // Walking backwards is flat too.
    w.host.set_axis("moveForward", -1.0);
    w.entities[p].loc = [0.0; 3];
    w.entities[p].rot = [45.0, 0.0, 0.0];
    run(&mut w, 1.0, 0.125);
    assert!(near(w.entities[p].loc[1], -5.0) && near(w.entities[p].loc[2], 0.0), "loc was {:?}", w.entities[p].loc);
}

#[test]
fn jump_fires_once_per_press_not_once_per_frame() {
    let mut w = world("player.prop");
    let p = w.spawn_entry([0.0; 3]);

    w.host.press("jump");
    run(&mut w, 0.5, 0.125); // held for four frames
    assert_eq!(int(&w, p, "JumpCount"), 1, "a held key is one press");
    assert!(near(w.entities[p].loc[2], 0.0), "the engine does the jumping, not the script");
    assert_eq!(count_calls(&w, "Audio.Play sound=\"jump.wav\""), 1);
    assert_eq!(w.host.printed, vec!["Jumped 1".to_string()]);

    w.host.release("jump");
    w.step(0.125);
    w.host.press("jump");
    w.step(0.125);
    assert_eq!(int(&w, p, "JumpCount"), 2);
    assert_eq!(w.host.printed.last().unwrap(), "Jumped 2");
}

#[test]
fn overlap_begin_and_end_open_and_close_the_door_and_the_variable_change_plays_the_sound() {
    let mut w = world("door.prop");
    let player_key = w.file_key("player.prop").unwrap();
    let door = w.spawn_entry([0.0; 3]);
    let player = w.spawn(&player_key, [0.0; 3]);

    w.touch(door, player);
    assert_eq!(w.get(door, "IsOpen"), Some(&Val::Bool(true)));
    assert_eq!(count_calls(&w, "Audio.Play sound=\"door.wav\""), 1, "one sound when it starts to open");

    run(&mut w, 1.0, 0.125); // OpenSpeed 1.5/s reaches 1 well within a second
    assert!(near(dec(&w, door, "Progress"), 1.0));
    assert!(near(w.entities[door].loc[2], 3.0), "fully open height was {}", w.entities[door].loc[2]);
    assert_eq!(count_calls(&w, "Audio.Play sound=\"door.wav\""), 1, "no repeat while it stays open");

    w.untouch(door, player);
    assert_eq!(w.get(door, "IsOpen"), Some(&Val::Bool(false)));
    assert_eq!(count_calls(&w, "Audio.Play sound=\"door.wav\""), 2, "one more when it starts to close");
    run(&mut w, 1.0, 0.125);
    assert!(near(w.entities[door].loc[2], 0.0), "closed height was {}", w.entities[door].loc[2]);
}

#[test]
fn a_door_can_be_placed_with_its_own_exposed_settings() {
    let mut w = world("door.prop");
    let player_key = w.file_key("player.prop").unwrap();
    let door_key = w.file_key("door.prop").unwrap();
    let door = w.spawn_with(&door_key, [0.0; 3], &[("OpenHeight".into(), Val::Dec(5.0)), ("OpenSpeed".into(), Val::Dec(10.0))]);
    let player = w.spawn(&player_key, [0.0; 3]);
    w.touch(door, player);
    run(&mut w, 0.25, 0.125);
    assert!(near(w.entities[door].loc[2], 5.0), "height was {}", w.entities[door].loc[2]);
    assert!(w.warnings.is_empty());
}

#[test]
fn setting_a_value_that_is_not_exposed_is_skipped_with_a_warning() {
    let mut w = world("door.prop");
    let door_key = w.file_key("door.prop").unwrap();
    let door = w.spawn_with(&door_key, [0.0; 3], &[("IsOpen".into(), Val::Bool(true))]);
    assert_eq!(w.get(door, "IsOpen"), Some(&Val::Bool(false)));
    assert!(w.warnings.iter().any(|m| m.contains("isn't exposed")), "{:?}", w.warnings);
}

#[test]
fn a_pickup_heals_the_player_and_disappears() {
    let mut w = world("health_pickup.prop");
    let player_key = w.file_key("player.prop").unwrap();
    let pickup = w.spawn_entry([0.0; 3]);
    let player = w.spawn(&player_key, [0.0; 3]);
    w.set(player, "Health", Val::Int(50));

    w.touch(pickup, player);
    assert_eq!(int(&w, player, "Health"), 75);
    assert!(!w.is_alive(pickup));
    assert_eq!(count_calls(&w, "Audio.Play sound=\"pickup.wav\""), 1);

    // Healing is capped at MaxHealth.
    let second = w.spawn(&w.file_key("health_pickup.prop").unwrap(), [0.0; 3]);
    w.touch(second, player);
    w.set(player, "Health", Val::Int(90));
    let third = w.spawn(&w.file_key("health_pickup.prop").unwrap(), [0.0; 3]);
    w.touch(third, player);
    assert_eq!(int(&w, player, "Health"), 100);
}

// ---- actions, math, and flow ----

#[test]
fn rotate_object_uses_the_exposed_speed() {
    let mut w = world("rotator.prop");
    let key = w.file_key("rotator.prop").unwrap();
    let slow = w.spawn(&key, [0.0; 3]);
    let fast = w.spawn_with(&key, [0.0; 3], &[("SpinSpeed".into(), Val::Dec(90.0))]);
    run(&mut w, 2.0, 0.25);
    assert!(near(w.entities[slow].rot[2], 90.0), "slow was {}", w.entities[slow].rot[2]);
    assert!(near(w.entities[fast].rot[2], 180.0), "fast was {}", w.entities[fast].rot[2]);
    // The 2-second print fired once for each, at t=2.
    assert_eq!(w.host.printed.len(), 2, "{:?}", w.host.printed);
    assert_eq!(w.host.printed[0], "Rotation is 90");
}

#[test]
fn lerp_moves_a_platform_back_and_forth() {
    let mut w = world("moving_platform.prop");
    let p = w.spawn_entry([0.0; 3]);
    let y = |w: &World<StubHost>| w.entities[p].loc[1];

    run(&mut w, 2.0, 0.25);
    assert!(near(y(&w), 5.0), "halfway out, y was {}", y(&w));
    run(&mut w, 2.0, 0.25);
    assert!(near(y(&w), 10.0), "at the far point, y was {}", y(&w));
    assert_eq!(w.get(p, "Returning"), Some(&Val::Bool(true)));
    run(&mut w, 2.0, 0.25);
    assert!(near(y(&w), 5.0), "halfway back, y was {}", y(&w));
    run(&mut w, 2.0, 0.25);
    assert!(near(y(&w), 0.0), "home again, y was {}", y(&w));
}

#[test]
fn vectors_scale_and_move_per_second() {
    let mut w = world("drifter.prop");
    let key = w.file_key("drifter.prop").unwrap();
    let a = w.spawn(&key, [0.0; 3]);
    let b = w.spawn_with(&key, [0.0; 3], &[("Direction".into(), Val::Vec3([0.0, 1.0, 0.0])), ("Speed".into(), Val::Dec(4.0))]);
    run(&mut w, 1.0, 0.125);
    assert!(near(w.entities[a].loc[0], 2.0), "default drift was {:?}", w.entities[a].loc);
    assert!(near(w.entities[b].loc[1], 4.0), "custom drift was {:?}", w.entities[b].loc);
}

#[test]
fn delay_waits_then_runs_with_the_names_it_had_and_a_loop_spawns_with_an_index() {
    let mut w = world("trap.prop");
    let player_key = w.file_key("player.prop").unwrap();
    let trap = w.spawn_entry([0.0; 3]);
    let player = w.spawn(&player_key, [0.0; 3]);

    w.touch(trap, player);
    assert_eq!(w.host.printed, vec!["Trap triggered".to_string()]);
    run(&mut w, 1.5, 0.5);
    assert_eq!(int(&w, player, "Health"), 100, "nothing yet before the two seconds are up");

    run(&mut w, 0.5, 0.5); // t = 2.0
    assert_eq!(int(&w, player, "Health"), 80, "the delayed block still knew who the victim was");
    assert_eq!(w.host.printed.last().unwrap(), "Trap sprung");

    let coin_key = w.file_key("coin.prop").unwrap();
    let mut xs: Vec<f32> = w.entities.iter().filter(|e| e.file == coin_key).map(|e| e.loc[0]).collect();
    xs.sort_by(|a, b| a.partial_cmp(b).unwrap());
    assert_eq!(xs, vec![0.0, 2.0, 4.0, 6.0, 8.0], "five coins, spaced by the loop index");

    // Stepping on an armed trap again does nothing.
    w.touch(trap, player);
    run(&mut w, 2.5, 0.5);
    assert_eq!(int(&w, player, "Health"), 80);
}

#[test]
fn a_delayed_block_is_dropped_if_its_entity_is_gone() {
    let mut w = world("trap.prop");
    let player_key = w.file_key("player.prop").unwrap();
    let trap = w.spawn_entry([0.0; 3]);
    let player = w.spawn(&player_key, [0.0; 3]);
    w.touch(trap, player);
    w.entities[trap].alive = false;
    run(&mut w, 3.0, 0.5);
    assert_eq!(int(&w, player, "Health"), 100);
}

#[test]
fn game_state_reacts_to_collected_coins_through_variable_changes() {
    let mut w = world("game_state.prop");
    let player_key = w.file_key("player.prop").unwrap();
    let coin_key = w.file_key("coin.prop").unwrap();
    let state = w.spawn_entry([0.0; 3]);
    let player = w.spawn(&player_key, [0.0; 3]);
    let coins: Vec<usize> = (0..3).map(|i| w.spawn(&coin_key, [i as f32, 0.0, 0.0])).collect();

    for (n, coin) in coins.iter().enumerate() {
        w.touch(*coin, player);
        assert_eq!(int(&w, state, "Score"), n as i32 + 1);
        let text = format!("UI.SetText element=\"score\" text=\"Score: {}\"", n + 1);
        assert!(w.host.calls.contains(&text), "missing {text} in {:?}", w.host.calls);
    }
    assert_eq!(count_calls(&w, "UI.Show element=\"win\""), 1, "only the third coin wins");
    assert_eq!(w.host.printed, vec!["You win!".to_string()]);
}

#[test]
fn the_player_dies_through_an_action_and_its_event() {
    let mut w = world("player.prop");
    let p = w.spawn_entry([0.0; 3]);
    w.call(p, "TakeDamage", vec![("amount".into(), Val::Int(40))]);
    assert_eq!(int(&w, p, "Health"), 60);
    w.call(p, "TakeDamage", vec![("amount".into(), Val::Int(500))]);
    assert_eq!(int(&w, p, "Health"), 0);
    assert_eq!(w.host.printed, vec!["The player died".to_string()]);
    assert_eq!(count_calls(&w, "UI.Show element=\"game over\""), 1);
}
