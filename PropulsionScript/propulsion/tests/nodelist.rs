//! The nodes taken from NODELIST.md: each one runs in a world with a stub engine and does what its name says.
//! (`behaviors/*_kit.prop` have one action per node; `cpp/tests/differential_kits.cpp` runs the same checks
//! against the generated C++.)

use propulsion::runtime::{StubHost, Val, World};
use propulsion::Project;
use std::path::PathBuf;

fn behaviors() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join("behaviors")
}

fn world(entry: &str) -> World<StubHost> {
    let project = Project::load(&behaviors().join(entry)).unwrap();
    World::new(project, StubHost::default()).unwrap_or_else(|reports| {
        let shown: String = reports.iter().flat_map(|r| r.errors.iter().map(move |e| e.render(&r.source, &r.path))).collect();
        panic!("behaviors should pass the checker:\n{shown}")
    })
}

fn near(a: f32, b: f32) -> bool {
    (a - b).abs() < 0.001
}

fn ask(w: &mut World<StubHost>, id: usize, action: &str, args: &[(&str, Val)]) -> Val {
    let args = args.iter().map(|(n, v)| (n.to_string(), v.clone())).collect();
    w.call(id, action, args).unwrap_or_else(|| panic!("`{action}` gave nothing back"))
}

fn run(w: &mut World<StubHost>, id: usize, action: &str, args: &[(&str, Val)]) {
    let args = args.iter().map(|(n, v)| (n.to_string(), v.clone())).collect();
    w.call(id, action, args);
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

fn vec3(v: Val) -> [f32; 3] {
    match v {
        Val::Vec3(v) => v,
        other => panic!("not a vector: {other:?}"),
    }
}

fn text(v: Val) -> String {
    match v {
        Val::Text(t) => t,
        other => panic!("not text: {other:?}"),
    }
}

fn boolean(v: Val) -> bool {
    match v {
        Val::Bool(b) => b,
        other => panic!("not a boolean: {other:?}"),
    }
}

fn integer(v: Val) -> i32 {
    match v {
        Val::Int(i) => i,
        other => panic!("not an integer: {other:?}"),
    }
}

fn decimal(v: Val) -> f32 {
    match v {
        Val::Dec(d) => d,
        other => panic!("not a decimal: {other:?}"),
    }
}

fn same(a: [f32; 3], b: [f32; 3]) -> bool {
    near(a[0], b[0]) && near(a[1], b[1]) && near(a[2], b[2])
}

// ---- math, vectors and logic ----

#[test]
fn clamp_keeps_a_number_between_two_others() {
    let mut w = world("math_kit.prop");
    let m = w.spawn_entry([0.0; 3]);
    assert_eq!(integer(ask(&mut w, m, "Limit", &[("value", Val::Int(15))])), 10);
    assert_eq!(integer(ask(&mut w, m, "Limit", &[("value", Val::Int(-3))])), 0);
    assert_eq!(integer(ask(&mut w, m, "Limit", &[("value", Val::Int(5))])), 5);
    assert!(near(decimal(ask(&mut w, m, "LimitDecimal", &[("value", Val::Dec(2.5))])), 1.0));
    assert!(near(decimal(ask(&mut w, m, "LimitDecimal", &[("value", Val::Dec(-2.5))])), -1.0));
}

#[test]
fn map_range_converts_between_ranges_and_stays_inside_them() {
    let mut w = world("math_kit.prop");
    let m = w.spawn_entry([0.0; 3]);
    assert!(near(decimal(ask(&mut w, m, "Scale", &[("speed", Val::Dec(5.0))])), 0.5));
    assert!(near(decimal(ask(&mut w, m, "Scale", &[("speed", Val::Dec(20.0))])), 1.0));
    assert!(near(decimal(ask(&mut w, m, "Scale", &[("speed", Val::Dec(-1.0))])), 0.0));
}

#[test]
fn random_numbers_stay_in_range_cover_it_and_repeat_for_the_same_seed() {
    let rolls = |seed: u64| {
        let mut w = world("math_kit.prop");
        w.seed(seed);
        let m = w.spawn_entry([0.0; 3]);
        (0..300).map(|_| integer(ask(&mut w, m, "Roll", &[]))).collect::<Vec<_>>()
    };
    let a = rolls(7);
    assert!(a.iter().all(|r| (1..=6).contains(r)), "{a:?}");
    for face in 1..=6 {
        assert!(a.contains(&face), "no {face} in 300 rolls");
    }
    assert_eq!(a, rolls(7), "the same seed rolls the same numbers");
    assert_ne!(a, rolls(8), "another seed rolls differently");

    let mut w = world("math_kit.prop");
    let m = w.spawn_entry([0.0; 3]);
    for _ in 0..100 {
        let d = decimal(ask(&mut w, m, "Jitter", &[]));
        assert!((5.0..6.0).contains(&d), "{d}");
    }
}

#[test]
fn vectors_can_be_normalized_measured_and_compared() {
    let mut w = world("math_kit.prop");
    let m = w.spawn_entry([0.0; 3]);
    assert!(same(vec3(ask(&mut w, m, "Unit", &[])), [0.6, 0.0, 0.8]));
    assert!(near(decimal(ask(&mut w, m, "Size", &[])), 5.0));
    assert!(near(decimal(ask(&mut w, m, "Alignment", &[("other", Val::Vec3([1.0, 0.0, 0.0]))])), 3.0));
    assert!(near(decimal(ask(&mut w, m, "Alignment", &[("other", Val::Vec3([0.0, 1.0, 0.0]))])), 0.0));
}

#[test]
fn a_vector_can_be_taken_apart_and_one_part_changed() {
    let mut w = world("math_kit.prop");
    let m = w.spawn_entry([0.0; 3]);
    assert!(near(decimal(ask(&mut w, m, "Sideways", &[])), 3.0), "Dir.X");
    run(&mut w, m, "Bend", &[("value", Val::Dec(2.5))]);
    assert!(same(vec3(w.get(m, "Dir").cloned().unwrap()), [3.0, 2.5, 4.0]), "only Y changed");
    assert!(near(decimal(ask(&mut w, m, "Heading", &[])), 20.0), "the yaw of a rotation is its Y");
}

#[test]
fn a_rotation_can_be_made_and_pointed_at_a_target() {
    let mut w = world("math_kit.prop");
    let m = w.spawn_entry([0.0; 3]);
    // `rotation pitch:10 yaw:20 roll:30` is stored as (pitch, roll, yaw): the turns about X, Y and Z.
    assert!(same(vec3(ask(&mut w, m, "Pose", &[])), [10.0, 30.0, 20.0]));
    // Forward is +Y: facing it is no turn at all, +X is a quarter turn to the right (yaw -90, about Z),
    // and up and ahead tilts up (pitch about X).
    assert!(same(vec3(ask(&mut w, m, "Facing", &[("target", Val::Vec3([0.0, 10.0, 0.0]))])), [0.0, 0.0, 0.0]));
    assert!(same(vec3(ask(&mut w, m, "Facing", &[("target", Val::Vec3([10.0, 0.0, 0.0]))])), [0.0, 0.0, -90.0]));
    assert!(same(vec3(ask(&mut w, m, "Facing", &[("target", Val::Vec3([0.0, 5.0, 5.0]))])), [45.0, 0.0, 0.0]));
    // Turning to that rotation really does make forward point at the target.
    let turned = vec3(ask(&mut w, m, "Facing", &[("target", Val::Vec3([10.0, 0.0, 0.0]))]));
    w.entities[m].rot = turned;
    assert!(same(vec3(ask(&mut w, m, "Ahead", &[])), [1.0, 0.0, 0.0]));
}

#[test]
fn forward_right_and_up_follow_where_the_entity_faces() {
    let mut w = world("math_kit.prop");
    let m = w.spawn_entry([0.0; 3]);
    // Z up, forward +Y, right +X.
    assert!(same(vec3(ask(&mut w, m, "Ahead", &[])), [0.0, 1.0, 0.0]));
    assert!(same(vec3(ask(&mut w, m, "Side", &[])), [1.0, 0.0, 0.0]));
    assert!(same(vec3(ask(&mut w, m, "Above", &[])), [0.0, 0.0, 1.0]));
    // A yaw of 90 degrees (about Z) turns the entity to face -X; up stays up.
    w.entities[m].rot = [0.0, 0.0, 90.0];
    assert!(same(vec3(ask(&mut w, m, "Ahead", &[])), [-1.0, 0.0, 0.0]));
    assert!(same(vec3(ask(&mut w, m, "Side", &[])), [0.0, 1.0, 0.0]));
    assert!(same(vec3(ask(&mut w, m, "Above", &[])), [0.0, 0.0, 1.0]));
}

#[test]
fn not_and_select_choose_by_a_boolean() {
    let mut w = world("math_kit.prop");
    let m = w.spawn_entry([0.0; 3]);
    assert!(!boolean(ask(&mut w, m, "Opposite", &[("value", Val::Bool(true))])));
    assert!(boolean(ask(&mut w, m, "Opposite", &[("value", Val::Bool(false))])));
    assert_eq!(text(ask(&mut w, m, "Label", &[])), "open");
    assert_eq!(integer(ask(&mut w, m, "Pick", &[("yes", Val::Int(1)), ("no", Val::Int(2))])), 1);
    w.set(m, "Ready", Val::Bool(false));
    assert_eq!(text(ask(&mut w, m, "Label", &[])), "closed");
    assert_eq!(integer(ask(&mut w, m, "Pick", &[("yes", Val::Int(1)), ("no", Val::Int(2))])), 2);
}

#[test]
fn easing_moves_part_of_the_way_each_frame_and_turning_takes_the_short_way() {
    let mut w = world("math_kit.prop");
    let m = w.spawn_entry([0.0; 3]);
    w.step(0.1);
    // speed 5 for a tenth of a second is half the distance
    assert!(near(decimal(ask(&mut w, m, "Slide", &[("current", Val::Dec(0.0))])), 5.0));
    assert!(same(vec3(ask(&mut w, m, "Glide", &[("current", Val::Vec3([0.0; 3]))])), [1.5, 0.0, 2.0]));
    // From 350 degrees toward 3 is a 13 degree turn forward, not 347 the other way; at speed 90 a hundredth
    // of a second is nine tenths of it.
    w.step(0.01);
    let turned = vec3(ask(&mut w, m, "Swing", &[("current", Val::Vec3([350.0, 0.0, 0.0]))]));
    assert!(same(turned, [361.7, 0.0, 3.6]), "{turned:?}");
    // A frame long enough to get there lands on the target (the short way round).
    w.step(1.0);
    let turned = vec3(ask(&mut w, m, "Swing", &[("current", Val::Vec3([350.0, 0.0, 0.0]))]));
    assert!(same(turned, [363.0, 0.0, 4.0]), "{turned:?}");
}

#[test]
fn level_name_and_format_text_read_the_world() {
    let mut w = world("math_kit.prop");
    w.host.level = "forest".to_string();
    let m = w.spawn_entry([0.0; 3]);
    assert_eq!(text(ask(&mut w, m, "Level", &[])), "forest");
    assert_eq!(text(ask(&mut w, m, "Describe", &[])), "Dir is (3, 0, 4) and ready is true");
}

// ---- lists ----

#[test]
fn list_nodes_add_unique_remove_at_clear_contains_and_find() {
    let mut w = world("list_kit.prop");
    let l = w.spawn_entry([0.0; 3]);
    for v in [1, 2, 2] {
        run(&mut w, l, "Add", &[("value", Val::Int(v))]);
    }
    assert_eq!(integer(ask(&mut w, l, "Total", &[])), 2, "a repeat isn't added a second time");
    run(&mut w, l, "AddAnyway", &[("value", Val::Int(2))]);
    assert_eq!(integer(ask(&mut w, l, "Total", &[])), 3, "plain add does allow repeats");
    assert!(boolean(ask(&mut w, l, "Has", &[("value", Val::Int(2))])));
    assert!(!boolean(ask(&mut w, l, "Has", &[("value", Val::Int(9))])));
    assert!(boolean(ask(&mut w, l, "Lacks", &[("value", Val::Int(9))])));
    assert!(!boolean(ask(&mut w, l, "Lacks", &[("value", Val::Int(1))])));
    assert_eq!(integer(ask(&mut w, l, "Where", &[("value", Val::Int(2))])), 1);
    assert_eq!(integer(ask(&mut w, l, "Where", &[("value", Val::Int(9))])), -1, "not found is -1");

    run(&mut w, l, "Drop", &[("index", Val::Int(0))]);
    assert_eq!(integer(ask(&mut w, l, "Where", &[("value", Val::Int(2))])), 0, "everything after it moved up");
    assert_eq!(integer(ask(&mut w, l, "Total", &[])), 2);
    assert!(w.warnings.is_empty());
    run(&mut w, l, "Drop", &[("index", Val::Int(99))]);
    assert_eq!(integer(ask(&mut w, l, "Total", &[])), 2, "a position past the end removes nothing");
    assert!(w.warnings.iter().any(|m| m.contains("past the end")), "{:?}", w.warnings);

    run(&mut w, l, "Wipe", &[]);
    assert_eq!(integer(ask(&mut w, l, "Total", &[])), 0);
}

// ---- flow ----

#[test]
fn do_once_runs_once_until_it_is_reset() {
    let mut w = world("flow_kit.prop");
    let f = w.spawn_entry([0.0; 3]);
    for _ in 0..3 {
        run(&mut w, f, "Greet", &[]);
    }
    assert_eq!(int(&w, f, "Hellos"), 1);
    run(&mut w, f, "Rearm", &[]);
    run(&mut w, f, "Greet", &[]);
    run(&mut w, f, "Greet", &[]);
    assert_eq!(int(&w, f, "Hellos"), 2, "reset let it run once more");
}

#[test]
fn do_n_runs_that_many_times_and_each_entity_counts_for_itself() {
    let mut w = world("flow_kit.prop");
    let a = w.spawn_entry([0.0; 3]);
    let b = w.spawn_entry([0.0; 3]);
    for _ in 0..5 {
        run(&mut w, a, "Warm", &[]);
    }
    assert_eq!(int(&w, a, "Warmups"), 3);
    run(&mut w, b, "Warm", &[]);
    assert_eq!(int(&w, b, "Warmups"), 1, "the other entity hasn't used any up");
    run(&mut w, a, "Rearm", &[]);
    for _ in 0..5 {
        run(&mut w, a, "Warm", &[]);
    }
    assert_eq!(int(&w, a, "Warmups"), 6);
}

#[test]
fn flip_flop_alternates_between_its_two_sides() {
    let mut w = world("flow_kit.prop");
    let f = w.spawn_entry([0.0; 3]);
    for _ in 0..5 {
        run(&mut w, f, "Step", &[]);
    }
    assert_eq!((int(&w, f, "Lefts"), int(&w, f, "Rights")), (3, 2));
}

#[test]
fn a_gate_lets_flow_through_only_while_it_is_open() {
    let mut w = world("flow_kit.prop");
    let f = w.spawn_entry([0.0; 3]);
    run(&mut w, f, "Pass", &[]);
    assert_eq!(int(&w, f, "Passes"), 0, "it starts closed");
    run(&mut w, f, "Unlock", &[]);
    run(&mut w, f, "Pass", &[]);
    assert_eq!(int(&w, f, "Passes"), 1);
    run(&mut w, f, "Lock", &[]);
    run(&mut w, f, "Pass", &[]);
    assert_eq!(int(&w, f, "Passes"), 1);
    run(&mut w, f, "Flip", &[]);
    run(&mut w, f, "Pass", &[]);
    assert_eq!(int(&w, f, "Passes"), 2, "toggling a closed gate opens it");
    run(&mut w, f, "Flip", &[]);
    run(&mut w, f, "Pass", &[]);
    assert_eq!(int(&w, f, "Passes"), 2, "and an open one closes");
}

#[test]
fn switch_picks_the_case_that_matches_or_otherwise() {
    let mut w = world("flow_kit.prop");
    let f = w.spawn_entry([0.0; 3]);
    let word = |w: &World<StubHost>| match w.get(f, "Word") {
        Some(Val::Text(t)) => t.clone(),
        other => panic!("{other:?}"),
    };
    for (n, expect) in [(1, "one"), (2, "few"), (3, "few"), (9, "many")] {
        run(&mut w, f, "Name", &[("value", Val::Int(n))]);
        assert_eq!(word(&w), expect, "for {n}");
    }
    run(&mut w, f, "Spell", &[("word", Val::Text("b".into()))]);
    assert_eq!(word(&w), "second");
    run(&mut w, f, "Spell", &[("word", Val::Text("zzz".into()))]);
    assert_eq!(word(&w), "second", "no case matched and there is no otherwise");
}

#[test]
fn a_timeline_runs_every_frame_for_its_time_and_then_stops() {
    let mut w = world("flow_kit.prop");
    let f = w.spawn_entry([0.0; 3]);
    run(&mut w, f, "Rise", &[]);
    assert_eq!(int(&w, f, "Frames"), 0, "it starts on the next frame");
    let mut heights = Vec::new();
    for _ in 0..4 {
        w.step(0.5);
        heights.push(w.entities[f].loc[2]);
    }
    assert!(heights.iter().zip([1.0, 2.0, 3.0, 4.0]).all(|(a, b)| near(*a, b)), "{heights:?}");
    assert_eq!(int(&w, f, "Frames"), 4);
    w.entities[f].loc[2] = 50.0;
    w.step(0.5);
    assert_eq!(int(&w, f, "Frames"), 4, "finished, so it doesn't run again");
    assert!(near(w.entities[f].loc[2], 50.0));
}

#[test]
fn a_timeline_can_move_a_vector() {
    let mut w = world("flow_kit.prop");
    let f = w.spawn_entry([0.0; 3]);
    run(&mut w, f, "Travel", &[]);
    w.step(0.25);
    assert!(same(w.entities[f].loc, [2.5, 0.0, 0.0]), "{:?}", w.entities[f].loc);
    w.step(0.75);
    assert!(same(w.entities[f].loc, [10.0, 0.0, 0.0]), "{:?}", w.entities[f].loc);
}

#[test]
fn a_timeline_stops_when_its_entity_is_destroyed() {
    let mut w = world("flow_kit.prop");
    let f = w.spawn_entry([0.0; 3]);
    run(&mut w, f, "Rise", &[]);
    w.step(0.5);
    assert_eq!(int(&w, f, "Frames"), 1);
    let frames = int(&w, f, "Frames");
    w.damage(f, None, 0.0); // a no-op on a script with no handler for it
    w.entities[f].alive = false;
    w.step(0.5);
    assert_eq!(int(&w, f, "Frames"), frames);
}

// ---- the world ----

fn pair() -> (World<StubHost>, usize, usize) {
    let mut w = world("world_kit.prop");
    let a = w.spawn_entry([0.0; 3]);
    let b = w.spawn_entry([10.0, 0.0, 0.0]);
    (w, a, b)
}

#[test]
fn damage_runs_the_targets_handler_with_the_amount_and_who_did_it() {
    let (mut w, a, b) = pair();
    run(&mut w, a, "Hurt", &[("target", Val::Entity(Some(b))), ("amount", Val::Dec(30.0))]);
    assert!(near(dec(&w, b, "Health"), 70.0));
    assert!(near(dec(&w, b, "LastHit"), 30.0));
    assert_eq!(w.get(b, "Attacker"), Some(&Val::Entity(Some(a))), "the handler knows who did it");
    assert!(near(dec(&w, a, "Health"), 100.0), "and the attacker is unhurt");
    // from outside the script
    w.damage(b, None, 5.0);
    assert!(near(dec(&w, b, "Health"), 65.0));
}

#[test]
fn damage_to_a_missing_entity_is_skipped_with_a_warning() {
    let (mut w, a, b) = pair();
    w.entities[b].alive = false;
    run(&mut w, a, "Hurt", &[("target", Val::Entity(Some(b))), ("amount", Val::Dec(30.0))]);
    assert!(w.warnings.iter().any(|m| m.contains("damage an entity that is missing")), "{:?}", w.warnings);
}

#[test]
fn visibility_and_collision_are_fields_an_entity_can_switch() {
    let (mut w, a, _) = pair();
    assert!(w.entities[a].shown && w.entities[a].collides);
    run(&mut w, a, "Hide", &[]);
    assert!(!w.entities[a].shown);
    run(&mut w, a, "Reveal", &[]);
    assert!(w.entities[a].shown);
    run(&mut w, a, "Ghost", &[]);
    assert!(!w.entities[a].collides);
}

#[test]
fn an_attached_entity_follows_its_parent_until_it_is_detached() {
    let (mut w, a, b) = pair();
    run(&mut w, a, "Follow", &[("target", Val::Entity(Some(b)))]);
    assert!(same(w.entities[a].loc, [0.0, 0.0, 0.0]), "attaching doesn't move it");
    w.entities[b].loc = [10.0, 0.0, 5.0];
    w.step(0.1);
    assert!(same(w.entities[a].loc, [0.0, 0.0, 5.0]), "{:?}", w.entities[a].loc);
    // turned a quarter, the offset turns with the parent: (-10, 0, 0) a yaw of 90 later is (0, 0, 10)
    w.entities[b].rot = [0.0, 90.0, 0.0];
    w.step(0.1);
    assert!(same(w.entities[a].loc, [10.0, 0.0, 15.0]), "{:?}", w.entities[a].loc);
    assert!(near(w.entities[a].rot[1], 90.0));
    run(&mut w, a, "Release", &[]);
    w.entities[b].loc = [99.0, 0.0, 0.0];
    w.step(0.1);
    assert!(same(w.entities[a].loc, [10.0, 0.0, 15.0]), "detached, it stays where it was");
}

#[test]
fn what_is_attached_to_a_destroyed_entity_stays_put() {
    let (mut w, a, b) = pair();
    run(&mut w, a, "Follow", &[("target", Val::Entity(Some(b)))]);
    w.entities[b].loc = [10.0, 0.0, 5.0];
    w.step(0.1);
    run(&mut w, b, "Hurt", &[("target", Val::Entity(Some(a))), ("amount", Val::Dec(1.0))]); // any call: b is alive
    w.entities[b].alive = false;
    w.step(0.1);
    assert!(same(w.entities[a].loc, [0.0, 0.0, 5.0]), "{:?}", w.entities[a].loc);
}

#[test]
fn two_entities_cannot_follow_each_other() {
    let (mut w, a, b) = pair();
    run(&mut w, a, "Follow", &[("target", Val::Entity(Some(b)))]);
    run(&mut w, b, "Follow", &[("target", Val::Entity(Some(a)))]);
    assert!(w.warnings.iter().any(|m| m.contains("follow each other")), "{:?}", w.warnings);
    run(&mut w, a, "Follow", &[("target", Val::Entity(Some(a)))]);
    assert!(w.warnings.iter().any(|m| m.contains("attached to itself")), "{:?}", w.warnings);
}

#[test]
fn tell_runs_an_action_by_name_and_skips_quietly_when_there_is_none() {
    let (mut w, a, b) = pair();
    run(&mut w, a, "Poke", &[("target", Val::Entity(Some(b)))]);
    assert_eq!(int(&w, b, "Pings"), 3);
    // an entity running a script with no `Ping` ignores it, and that isn't worth a warning
    let math = w.file_key("math_kit.prop").unwrap();
    let m = w.spawn(&math, [0.0; 3]);
    run(&mut w, a, "Poke", &[("target", Val::Entity(Some(m)))]);
    assert!(w.warnings.is_empty(), "{:?}", w.warnings);
}

#[test]
fn cast_gives_the_entity_when_it_runs_that_script_and_a_missing_one_otherwise() {
    let (mut w, a, b) = pair();
    let math = w.file_key("math_kit.prop").unwrap();
    let m = w.spawn(&math, [0.0; 3]);
    assert!(boolean(ask(&mut w, a, "IsPad", &[("target", Val::Entity(Some(m)))])));
    assert!(!boolean(ask(&mut w, a, "IsPad", &[("target", Val::Entity(Some(b)))])));
    assert!(!boolean(ask(&mut w, a, "IsPad", &[("target", Val::Entity(None))])));
}

#[test]
fn a_hit_runs_the_hit_handler_of_an_entity_that_listens_for_that_script() {
    let (mut w, a, b) = pair();
    let math = w.file_key("math_kit.prop").unwrap();
    let m = w.spawn(&math, [0.0; 3]);
    w.hit(a, m);
    assert_eq!(int(&w, a, "Bumps"), 1);
    w.hit(a, b);
    assert_eq!(int(&w, a, "Bumps"), 1, "only entities running the named script count");
    w.touch(a, m);
    assert_eq!(int(&w, a, "Bumps"), 1, "touching is not hitting");
}

#[test]
fn system_nodes_pass_their_optional_arguments_to_the_host() {
    let (mut w, a, _) = pair();
    run(&mut w, a, "Effects", &[]);
    let calls = w.host.calls.clone();
    assert_eq!(
        calls,
        vec![
            "Audio.Play sound=\"boom.wav\" at=(0, 0, 0) volume=0.5",
            "Audio.Play sound=\"hum.wav\" attachedTo=entity #0",
            "Animation.Play animation=\"walk\" loop=true",
            "Animation.PlayMontage montage=\"attack\" section=\"swing\"",
            "UI.Create widget=\"hud\"",
            "UI.Remove element=\"hud\"",
            "Game.OpenLevel level=\"forest\"",
            "Game.SetPaused paused=true",
        ]
    );
}
