//! Enums, structs, tables and traces (see `behaviors/types_kit.forge`): each runs in a world with a stub engine.
//! `cpp/tests/differential_kits.cpp` runs the same checks against the generated C++.

use forge::runtime::{StubHost, Val, World};
use forge::Project;
use std::path::PathBuf;

fn world(entry: &str) -> World<StubHost> {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join("behaviors").join(entry);
    let project = Project::load(&path).unwrap();
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

fn vec3(v: Val) -> [f32; 3] {
    match v {
        Val::Vec3(v) => v,
        other => panic!("not a vector: {other:?}"),
    }
}

fn same(a: [f32; 3], b: [f32; 3]) -> bool {
    near(a[0], b[0]) && near(a[1], b[1]) && near(a[2], b[2])
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

fn mode(w: &World<StubHost>, id: usize, var: &str) -> String {
    match w.get(id, var) {
        Some(Val::Enum(_, v)) => v.clone(),
        other => panic!("{var} is not an enum: {other:?}"),
    }
}

#[test]
fn an_enum_starts_as_its_first_value_can_be_changed_compared_and_switched_on() {
    let mut w = world("types_kit.forge");
    let k = w.spawn_entry([0.0; 3]);
    assert_eq!(mode(&w, k, "State"), "Idle", "an enum starts as its first value");
    assert_eq!(text(ask(&mut w, k, "Describe", &[])), "idle");
    assert!(boolean(ask(&mut w, k, "IsIdle", &[])));
    run(&mut w, k, "Chase", &[]);
    assert_eq!(mode(&w, k, "State"), "Chasing");
    assert_eq!(text(ask(&mut w, k, "Describe", &[])), "chasing");
    assert!(!boolean(ask(&mut w, k, "IsIdle", &[])));
    assert_eq!(text(ask(&mut w, k, "Label", &[])), "Chasing", "an enum's text is the name of its value");
}

#[test]
fn a_table_row_is_a_struct_whose_fields_can_be_read_and_changed() {
    let mut w = world("types_kit.forge");
    let k = w.spawn_entry([0.0; 3]);
    run(&mut w, k, "Pick", &[("key", Val::Text("Sword".into()))]);
    assert_eq!(integer(ask(&mut w, k, "Price", &[])), 5);
    assert_eq!(ask(&mut w, k, "Stance", &[]), Val::Enum("Mode".into(), "Attacking".into()), "a row can set an enum field");
    run(&mut w, k, "Pick", &[("key", Val::Text("Shield".into()))]);
    assert_eq!(integer(ask(&mut w, k, "Price", &[])), 8);
    assert_eq!(ask(&mut w, k, "Stance", &[]), Val::Enum("Mode".into(), "Idle".into()), "a field a row leaves out is its default");
    assert_eq!(integer(ask(&mut w, k, "Count", &[])), 2, "structs can go in a list");
    assert!(w.warnings.is_empty(), "{:?}", w.warnings);

    run(&mut w, k, "Pick", &[("key", Val::Text("Nothing".into()))]);
    assert_eq!(integer(ask(&mut w, k, "Price", &[])), 0, "a missing row is the defaults");
    assert!(w.warnings.iter().any(|m| m.contains("has no row \"Nothing\"")), "{:?}", w.warnings);
}

#[test]
fn a_struct_can_be_made_and_one_field_changed_at_a_time() {
    let mut w = world("types_kit.forge");
    let k = w.spawn_entry([0.0; 3]);
    run(&mut w, k, "Craft", &[("cost", Val::Int(7))]);
    assert_eq!(integer(ask(&mut w, k, "Price", &[])), 7);
    run(&mut w, k, "Discount", &[("amount", Val::Int(3))]);
    assert_eq!(integer(ask(&mut w, k, "Price", &[])), 4, "subtract works on a field");
    run(&mut w, k, "Rename", &[("word", Val::Text("Lance".into()))]);
    run(&mut w, k, "Shift", &[("distance", Val::Dec(2.5))]);
    match w.get(k, "Held") {
        Some(Val::Struct(name, fields)) => {
            assert_eq!(name, "Item");
            let field = |n: &str| fields.iter().find(|(f, _)| f == n).map(|(_, v)| v.clone()).unwrap();
            assert_eq!(field("Name"), Val::Text("Lance".into()));
            assert_eq!(field("Cost"), Val::Int(4));
            assert!(same(vec3(field("Spot")), [2.5, 0.0, 0.0]), "a part of a vector inside a struct");
        }
        other => panic!("{other:?}"),
    }
}

fn tracer() -> (World<StubHost>, usize, usize, usize) {
    let mut w = world("types_kit.forge");
    let me = w.spawn_entry([0.0; 3]);
    let key = w.file_key("types_kit.forge").unwrap();
    let near_one = w.spawn(&key, [0.0, 0.0, -5.0]);
    let far_one = w.spawn(&key, [0.0, 0.0, -10.0]);
    (w, me, near_one, far_one)
}

#[test]
fn a_trace_reports_what_it_hits_first_and_where() {
    let (mut w, me, near_one, far_one) = tracer();
    run(&mut w, me, "Scan", &[("start", Val::Vec3([0.0, 0.0, 0.0])), ("end", Val::Vec3([0.0, 0.0, -20.0]))]);
    assert!(boolean(ask(&mut w, me, "Seen", &[])));
    assert_eq!(ask(&mut w, me, "Who", &[]), Val::Entity(Some(near_one)), "the nearer of two in line");
    assert!(near(decimal(ask(&mut w, me, "HowFar", &[])), 4.5), "the front of a ball half a unit wide");
    assert!(same(vec3(ask(&mut w, me, "Surface", &[])), [0.0, 0.0, 1.0]), "its surface faces the one who looked");

    // Nothing in the way.
    run(&mut w, me, "Scan", &[("start", Val::Vec3([0.0, 0.0, -20.0])), ("end", Val::Vec3([0.0, 0.0, -30.0]))]);
    assert!(!boolean(ask(&mut w, me, "Seen", &[])));
    assert_eq!(ask(&mut w, me, "Who", &[]), Val::Entity(None));
    assert!(near(decimal(ask(&mut w, me, "HowFar", &[])), 10.0), "no hit reports the whole length");

    // Something with its collision turned off isn't there.
    w.entities[near_one].collides = false;
    run(&mut w, me, "Scan", &[("start", Val::Vec3([0.0, 0.0, 0.0])), ("end", Val::Vec3([0.0, 0.0, -20.0]))]);
    assert_eq!(ask(&mut w, me, "Who", &[]), Val::Entity(Some(far_one)));
    assert!(near(decimal(ask(&mut w, me, "HowFar", &[])), 9.5));
}

#[test]
fn a_thick_trace_hits_what_a_thin_one_misses() {
    let (mut w, me, near_one, _) = tracer();
    let beside = [("start", Val::Vec3([3.0, 0.0, 0.0])), ("end", Val::Vec3([3.0, 0.0, -20.0]))];
    let thin: Vec<(&str, Val)> = beside.iter().cloned().chain([("width", Val::Dec(1.0))]).collect();
    run(&mut w, me, "Sweep", &thin);
    assert!(!boolean(ask(&mut w, me, "Seen", &[])), "a line 3 to the side misses a ball half a unit wide, even a unit thick");
    let thick: Vec<(&str, Val)> = beside.iter().cloned().chain([("width", Val::Dec(3.0))]).collect();
    run(&mut w, me, "Sweep", &thick);
    assert_eq!(ask(&mut w, me, "Who", &[]), Val::Entity(Some(near_one)));
    assert!(near(decimal(ask(&mut w, me, "HowFar", &[])), 5.0 - 3.25f32.sqrt()));
}
